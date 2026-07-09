"""
Joystick introduction task.

Goal:
- Teach joystick-to-cursor agency with a simple visual cursor and target.
- Cursor is drawn inside the task canvas only (does not control system mouse).

Two control modes:
- cumulative: joystick acts like velocity input (mouse-like accumulation).
- direct: joystick directly maps to cursor position around center.

Full task description, definitions, and overall design can be read in 
docs/joystick_intro_task_guide.md. Any changes done to this task should be 
reflected on the accompanying md file.

Testing:
 > python -m thalamus.task_controller --pypipeline -c joy_home_test.json

 for mac: ❯ .venv/bin/python -m pip install --force-reinstall PyQt6 PyQt6-Qt6 PyQt6-sip
 
"""

import asyncio
import datetime
import logging
import math
import os
import random
import time
import typing

from ..qt import *
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from .widgets import Form
from .util import create_task_with_exc_handling, CanvasPainterProtocol, TaskContextProtocol, TaskResult, RenderOutput, get_sound
from ..config import *

LOGGER = logging.getLogger(__name__)

DEFAULT_TARGET_RADIUS_RATIO = 0.08
DEFAULT_TARGET_COLOR = [0, 220, 60]
DEFAULT_TARGET_OPACITY = 1.0
DEFAULT_TARGET_ACTIVE_COLOR = [255, 255, 255]
DEFAULT_TARGET_ACTIVE_OPACITY = 1.0
DEFAULT_TARGET_HOLD_TIME = 0.40
KEYBOARD_JOYSTICK_MAGNITUDE = 1.0
OPERATOR_KEYBOARD_CURSOR_SPEED = 0.85
DEFAULT_MAX_LOGGED_JOYSTICK_SAMPLES = 2000

def toggle_brightness(brightness: int) -> int:
  return 0 if brightness == 255 else 255

def clamp_float(v: typing.Any, lo: float, hi: float, default: float) -> float:
  try:
    value = float(v)
  except (TypeError, ValueError):
    value = default
  return max(lo, min(hi, value))

def normalize_rgb(value: typing.Any, default: typing.Sequence[int]) -> typing.List[int]:
  if isinstance(value, QColor):
    return [value.red(), value.green(), value.blue()]
  if isinstance(value, (list, tuple)) and len(value) >= 3:
    try:
      return [
        max(0, min(255, int(value[0]))),
        max(0, min(255, int(value[1]))),
        max(0, min(255, int(value[2]))),
      ]
    except (TypeError, ValueError):
      pass
  return [int(default[0]), int(default[1]), int(default[2])]

def rename_in_schedule(schedule: typing.Dict[str, typing.Any], old_name: str, new_name: str) -> None:
  """Rewrite every reference to a renamed target in a target_schedule dict.

  Targets are referenced by name in `center`, `peripherals`, and `order`; call
  this whenever a target is renamed so the schedule keeps pointing at it.
  """
  if schedule.get("center", "") == old_name:
    schedule["center"] = new_name
  for key in ("peripherals", "order"):
    names = schedule.get(key, [])
    if isinstance(names, list):
      schedule[key] = [new_name if name == old_name else name for name in names]


def prune_schedule_names(schedule: typing.Dict[str, typing.Any], live_names: typing.Set[str]) -> None:
  """Drop schedule references to targets that no longer exist."""
  if schedule.get("center", "") not in live_names:
    schedule["center"] = ""
  peripherals = schedule.get("peripherals", [])
  if isinstance(peripherals, list):
    schedule["peripherals"] = [name for name in peripherals if name in live_names]


def schedule_pattern_preview(
  schedule: typing.Dict[str, typing.Any],
  targets: typing.List[typing.Dict[str, typing.Any]],
  slots: int = 8,
) -> str:
  """Deterministic human-readable preview of the structured target stream."""
  mode = str(schedule.get("mode", "random"))
  enabled_names = [
    str(target.get("name", ""))
    for target in targets
    if isinstance(target, dict) and bool(target.get("enabled", True)) and str(target.get("name", "")).strip()
  ]
  ratio = clamp_float(schedule.get("interleave_random_ratio", 0.0), 0.0, 1.0, 0.0)
  if mode == "sequence":
    order = [str(name) for name in schedule.get("order", []) if str(name).strip()]
    if not order:
      return "Sequence is empty — every trial falls back to a random draw."
    pattern = [order[i % len(order)] for i in range(slots)]
  elif mode == "center_out":
    center = str(schedule.get("center", ""))
    ring = [str(name) for name in schedule.get("peripherals", []) if str(name).strip()]
    if not ring:
      ring = [name for name in enabled_names if name != center]
    if not center.strip():
      return "No center target chosen — center slots fall back to random draws."
    if not ring:
      return "No peripheral targets — peripheral slots fall back to random draws."
    random_ring = str(schedule.get("peripheral_order", "sequential")) == "random"
    pattern = []
    ring_pos = 0
    for i in range(slots):
      if i % 2 == 0:
        pattern.append(center)
      elif random_ring:
        pattern.append("?")
      else:
        pattern.append(ring[ring_pos % len(ring)])
        ring_pos += 1
  else:
    return "Random mode — every enabled target is equally likely; no structured pattern."
  text = " → ".join(pattern) + " → …"
  if ratio >= 1.0:
    text += "   |   100% random inserts — structured slots never run"
  elif ratio > 0.0:
    if mode == "center_out":
      text += f"   |   {ratio:.0%} random inserts between pairs (a pair is never split)"
    else:
      text += f"   |   {ratio:.0%} of trials are random inserts (pattern resumes after)"
  return text


def color_with_opacity(rgb: typing.Sequence[int], opacity: float) -> QColor:
  color = QColor(int(rgb[0]), int(rgb[1]), int(rgb[2]))
  color.setAlpha(int(round(255 * clamp_float(opacity, 0.0, 1.0, 1.0))))
  return color

def create_widget(task_config: ObservableCollection) -> QWidget:
  class NoWheelChangeFilter(QWidget):
    def eventFilter(self, watched: typing.Any, event: typing.Any) -> bool:
      # Avoid direct QEvent dependency because some qt wrappers in this repo
      # do not export QEvent symbols.
      if hasattr(event, "angleDelta"):
        if isinstance(watched, (QSpinBox, QDoubleSpinBox, QComboBox, QSlider)):
          event.ignore()
          return True
      return super().eventFilter(watched, event)

  result = QWidget()
  layout = QVBoxLayout(result)
  result.setLayout(layout)

  if "control_mode" not in task_config:
    task_config["control_mode"] = "direct"
  if "cursor_only_mode" not in task_config:
    task_config["cursor_only_mode"] = False
  if "free_play_end_key" not in task_config:
    task_config["free_play_end_key"] = "space"
  if "cursor_diameter_ratio" not in task_config:
    task_config["cursor_diameter_ratio"] = 0.1
  if "reset_cursor_each_trial" not in task_config:
    task_config["reset_cursor_each_trial"] = True
  if "require_center_before_trial" not in task_config:
    task_config["require_center_before_trial"] = False
  if "center_gate_radius_ratio" not in task_config:
    task_config["center_gate_radius_ratio"] = 0.15
  if "direct_recenter_when_idle" not in task_config:
    task_config["direct_recenter_when_idle"] = True
  if "task_region_x" not in task_config:
    task_config["task_region_x"] = 0.5
  if "task_region_y" not in task_config:
    task_config["task_region_y"] = 0.270
  if "task_region_width" not in task_config:
    task_config["task_region_width"] = 0.5
  if "task_region_height" not in task_config:
    task_config["task_region_height"] = 0.67
  if "trial_timeout" not in task_config:
    task_config["trial_timeout"] = 0.5
  if "state_indicator_x" not in task_config:
    task_config["state_indicator_x"] = 30
  if "state_indicator_y" not in task_config:
    task_config["state_indicator_y"] = 70
  if "ignore_idle_trial_failures" not in task_config:
    if "pause_timeout_while_idle" in task_config:
      task_config["ignore_idle_trial_failures"] = bool(task_config["pause_timeout_while_idle"])
    else:
      task_config["ignore_idle_trial_failures"] = False
  if "ignored_idle_sample_clear_threshold" not in task_config:
    task_config["ignored_idle_sample_clear_threshold"] = 50
  if "max_logged_joystick_samples" not in task_config:
    task_config["max_logged_joystick_samples"] = DEFAULT_MAX_LOGGED_JOYSTICK_SAMPLES
  if "fail_on_touch_input" not in task_config:
    task_config["fail_on_touch_input"] = False
  if "animations_enabled" not in task_config:
    task_config["animations_enabled"] = False
  if "task_animation_enabled" not in task_config:
    task_config["task_animation_enabled"] = True
  if "target_animation_enabled" not in task_config:
    task_config["target_animation_enabled"] = True
  if "show_streak_hud" not in task_config:
    task_config["show_streak_hud"] = True
  if "streak_bonus_threshold" not in task_config:
    task_config["streak_bonus_threshold"] = 0
  if "streak_bonus_reward_count" not in task_config:
    task_config["streak_bonus_reward_count"] = 1
  if "streak_reset_on_bonus" not in task_config:
    task_config["streak_reset_on_bonus"] = False
  if "inside_tint_strength_pct" not in task_config:
    task_config["inside_tint_strength_pct"] = 20
  if "show_hold_progress_ring" not in task_config:
    task_config["show_hold_progress_ring"] = True
  if "show_success_pop" not in task_config:
    task_config["show_success_pop"] = True
  if "show_success_particles" not in task_config:
    task_config["show_success_particles"] = True
  if "success_pop_duration_s" not in task_config:
    task_config["success_pop_duration_s"] = 0.12
  legacy_free_play_reward_enabled = bool(task_config.get("free_play_reward_enabled", False))
  legacy_free_play_reward_policy = str(task_config.get("free_play_reward_policy", "touch_bouts"))
  legacy_free_play_reward_channel = int(task_config.get("free_play_reward_channel", task_config.get("reward_channel", 0)))
  legacy_free_play_reward_threshold = float(task_config.get("free_play_reward_threshold", 0.0))
  legacy_free_play_reward_cooldown_s = float(task_config.get("free_play_reward_cooldown_s", 1.0))
  if "free_play_active_threshold" not in task_config:
    task_config["free_play_active_threshold"] = legacy_free_play_reward_threshold
  if "free_play_first_touch_reward_enabled" not in task_config:
    task_config["free_play_first_touch_reward_enabled"] = legacy_free_play_reward_enabled and legacy_free_play_reward_policy == "first_touch"
  if "free_play_first_touch_reward_channel" not in task_config:
    task_config["free_play_first_touch_reward_channel"] = legacy_free_play_reward_channel
  if "free_play_bout_reward_enabled" not in task_config:
    task_config["free_play_bout_reward_enabled"] = legacy_free_play_reward_enabled and legacy_free_play_reward_policy == "touch_bouts"
  if "free_play_bout_reward_channel" not in task_config:
    task_config["free_play_bout_reward_channel"] = legacy_free_play_reward_channel
  if "free_play_bout_cooldown_s" not in task_config:
    task_config["free_play_bout_cooldown_s"] = legacy_free_play_reward_cooldown_s
  if "free_play_sustain_reward_enabled" not in task_config:
    task_config["free_play_sustain_reward_enabled"] = legacy_free_play_reward_enabled and legacy_free_play_reward_policy == "timed_active"
  if "free_play_sustain_reward_channel" not in task_config:
    task_config["free_play_sustain_reward_channel"] = legacy_free_play_reward_channel
  if "free_play_sustain_initial_delay_s" not in task_config:
    task_config["free_play_sustain_initial_delay_s"] = 0.0
  if "free_play_sustain_interval_s" not in task_config:
    task_config["free_play_sustain_interval_s"] = legacy_free_play_reward_cooldown_s

  form = Form.build(
    task_config, ["Parameter", "Value"],
    Form.String("Joystick Node", "joystick_node", "Joystick"),
    Form.Choice("Control Mode", "control_mode", [
      ("Cumulative", "cumulative"),
      ("Direct", "direct"),
    ]),
    Form.Constant("Cumulative Speed", "cumulative_speed", 0.70, precision=3),
    Form.Bool("Zero-Drift Mode", "zero_drift_mode", True),
    Form.Constant("Zero-Drift Buffer", "zero_drift_buffer", 0.05, precision=3),
    Form.Constant("Direct Range", "direct_range", 0.45, precision=3),
    Form.Bool("Direct Recenter When Idle", "direct_recenter_when_idle", True),
    Form.Constant("Cursor Diameter Ratio", "cursor_diameter_ratio", 0.1, precision=3),
    Form.Bool("Reset Cursor Each Trial", "reset_cursor_each_trial", True),
    Form.Bool("Require Center Before Trial", "require_center_before_trial", False),
    Form.Constant("Center Gate Radius Ratio", "center_gate_radius_ratio", 0.15, precision=3),
    Form.Color("Cursor Color", "cursor_color", QColor(255, 70, 70)),
    Form.Constant("Task Region Center X", "task_region_x", 0.5, precision=3),
    Form.Constant("Task Region Center Y", "task_region_y", 0.270, precision=3),
    Form.Constant("Task Region Width", "task_region_width", 0.5, precision=3),
    Form.Constant("Task Region Height", "task_region_height", 0.67, precision=3),
    Form.Constant("State Indicator Right Margin", "state_indicator_x", 30, precision=0),
    Form.Constant("State Indicator Bottom Margin", "state_indicator_y", 70, precision=0),
    Form.Constant("Reward Channel", "reward_channel", 0, precision=0),
    Form.Constant("Reward Scale", "reward_scale", 1.0, precision=3),
    Form.Constant("Trial Timeout (s)", "trial_timeout", 0.5, "s", precision=3),
    Form.Bool("Ignore Idle Trial Failures", "ignore_idle_trial_failures", False),
    Form.Constant("Idle Sample Clear Threshold", "ignored_idle_sample_clear_threshold", 50, precision=0),
    Form.Constant("Max Logged Joystick Samples", "max_logged_joystick_samples", DEFAULT_MAX_LOGGED_JOYSTICK_SAMPLES, precision=0),
    Form.Bool("Fail On Touch Input", "fail_on_touch_input", False),
    Form.Constant("Intertrial Interval (s)", "intertrial_interval", 1.0, "s", precision=3),
  )
  layout.addWidget(form)

  free_play_group = QGroupBox("Cursor-Only Free Play")
  free_play_group_layout = QVBoxLayout(free_play_group)
  free_play_enable_row = QWidget()
  free_play_enable_layout = QHBoxLayout(free_play_enable_row)
  free_play_enable_layout.setContentsMargins(0, 0, 0, 0)
  cursor_only_checkbox = QCheckBox("Enable Cursor-Only Free Play")
  cursor_only_checkbox.setObjectName("cursor_only_mode")
  cursor_only_checkbox.setChecked(bool(task_config.get("cursor_only_mode", False)))
  free_play_enable_layout.addWidget(cursor_only_checkbox)
  free_play_enable_layout.addStretch(1)
  free_play_group_layout.addWidget(free_play_enable_row)

  free_play_details = QWidget()
  free_play_layout = QGridLayout(free_play_details)
  free_play_layout.setContentsMargins(0, 0, 0, 0)
  free_play_layout.setColumnStretch(1, 1)

  def add_free_play_section(row: int, text: str) -> int:
    label = QLabel(text)
    font = QFont(label.font())
    font.setBold(True)
    label.setFont(font)
    free_play_layout.addWidget(label, row, 0, 1, 3)
    return row + 1

  def add_free_play_bool(row: int, label_text: str, config_key: str) -> int:
    box = QCheckBox()
    box.setChecked(bool(task_config.get(config_key, False)))
    box.toggled.connect(lambda v, k=config_key: task_config.update({k: bool(v)}))
    free_play_layout.addWidget(QLabel(label_text), row, 0)
    free_play_layout.addWidget(box, row, 1, 1, 2)
    return row + 1

  def add_free_play_channel(row: int, label_text: str, config_key: str) -> int:
    spin = QSpinBox()
    spin.setRange(0, 255)
    spin.setSingleStep(1)
    spin.setValue(max(0, min(255, int(task_config.get(config_key, task_config.get("reward_channel", 0))))))
    spin.valueChanged.connect(lambda v, k=config_key: task_config.update({k: int(v)}))
    free_play_layout.addWidget(QLabel(label_text), row, 0)
    free_play_layout.addWidget(spin, row, 1, 1, 2)
    return row + 1

  def add_free_play_seconds(row: int, label_text: str, config_key: str, default: float) -> int:
    spin = QDoubleSpinBox()
    spin.setRange(0.0, 1000.0)
    spin.setDecimals(3)
    spin.setSingleStep(0.05)
    spin.setSuffix(" s")
    spin.setValue(max(0.0, float(task_config.get(config_key, default))))
    spin.valueChanged.connect(lambda v, k=config_key: task_config.update({k: float(v)}))
    free_play_layout.addWidget(QLabel(label_text), row, 0)
    free_play_layout.addWidget(spin, row, 1, 1, 2)
    return row + 1

  def add_free_play_choice(row: int, label_text: str, config_key: str, options: typing.List[typing.Tuple[str, str]]) -> int:
    combo = QComboBox()
    for label, value in options:
      combo.addItem(label, value)
      if value == str(task_config.get(config_key, options[0][1])):
        combo.setCurrentIndex(combo.count() - 1)
    combo.currentIndexChanged.connect(lambda i, c=combo, k=config_key: task_config.update({k: c.itemData(i)}))
    free_play_layout.addWidget(QLabel(label_text), row, 0)
    free_play_layout.addWidget(combo, row, 1, 1, 2)
    return row + 1

  def add_free_play_threshold(row: int) -> int:
    slider = QSlider(Qt.Orientation.Horizontal)
    slider.setRange(0, 100)
    slider.setSingleStep(1)
    slider.setPageStep(5)
    value = max(0.0, min(1.0, float(task_config.get("free_play_active_threshold", 0.0))))
    slider.setValue(int(round(value * 100.0)))
    value_label = QLabel(f"{value:.2f}")
    value_label.setMinimumWidth(44)

    def on_threshold_changed(v: int) -> None:
      threshold = max(0.0, min(1.0, float(v) / 100.0))
      task_config.update({"free_play_active_threshold": threshold})
      value_label.setText(f"{threshold:.2f}")

    slider.valueChanged.connect(on_threshold_changed)
    free_play_layout.addWidget(QLabel("Active Threshold"), row, 0)
    free_play_layout.addWidget(slider, row, 1)
    free_play_layout.addWidget(value_label, row, 2)
    return row + 1

  free_play_row = 0
  free_play_row = add_free_play_choice(free_play_row, "End Key", "free_play_end_key", [
    ("Space", "space"),
    ("Q", "q"),
    ("Enter", "enter"),
  ])
  free_play_row = add_free_play_threshold(free_play_row)
  threshold_note = QLabel("0.00 uses the same movement threshold as zero-drift; higher values require stronger joystick modulation.")
  threshold_note.setWordWrap(True)
  free_play_layout.addWidget(threshold_note, free_play_row, 0, 1, 3)
  free_play_row += 1

  free_play_row = add_free_play_section(free_play_row, "First Touch")
  free_play_row = add_free_play_bool(free_play_row, "Reward First Touch", "free_play_first_touch_reward_enabled")
  free_play_row = add_free_play_channel(free_play_row, "First Touch Channel", "free_play_first_touch_reward_channel")

  free_play_row = add_free_play_section(free_play_row, "Bout Starts")
  free_play_row = add_free_play_bool(free_play_row, "Reward Each Bout Start", "free_play_bout_reward_enabled")
  free_play_row = add_free_play_channel(free_play_row, "Bout Channel", "free_play_bout_reward_channel")
  free_play_row = add_free_play_seconds(free_play_row, "Bout Cooldown", "free_play_bout_cooldown_s", 1.0)

  free_play_row = add_free_play_section(free_play_row, "Sustained Active")
  free_play_row = add_free_play_bool(free_play_row, "Reward While Active", "free_play_sustain_reward_enabled")
  free_play_row = add_free_play_channel(free_play_row, "Sustain Channel", "free_play_sustain_reward_channel")
  free_play_row = add_free_play_seconds(free_play_row, "Initial Delay", "free_play_sustain_initial_delay_s", 0.0)
  free_play_row = add_free_play_seconds(free_play_row, "Repeat Interval", "free_play_sustain_interval_s", 1.0)

  free_play_details.setVisible(bool(task_config.get("cursor_only_mode", False)))
  cursor_only_checkbox.toggled.connect(lambda v: task_config.update({"cursor_only_mode": bool(v)}))
  cursor_only_checkbox.toggled.connect(free_play_details.setVisible)
  free_play_group_layout.addWidget(free_play_details)
  layout.addWidget(free_play_group)

  # Direction influence (0-100%) for each cardinal direction.
  # Backwards compatibility with legacy disable_* booleans:
  # disabled direction => 0%, enabled direction => 100%.
  if "up_influence_pct" not in task_config:
    task_config["up_influence_pct"] = 0 if bool(task_config.get("disable_up", False)) else 100
  if "down_influence_pct" not in task_config:
    task_config["down_influence_pct"] = 0 if bool(task_config.get("disable_down", False)) else 100
  if "left_influence_pct" not in task_config:
    task_config["left_influence_pct"] = 0 if bool(task_config.get("disable_left", False)) else 100
  if "right_influence_pct" not in task_config:
    task_config["right_influence_pct"] = 0 if bool(task_config.get("disable_right", False)) else 100

  def make_direction_slider_row(
    label_text: str,
    config_key: str,
  ) -> QWidget:
    row = QWidget()
    row_layout = QHBoxLayout(row)
    row_layout.setContentsMargins(0, 0, 0, 0)
    row_layout.addWidget(QLabel(label_text))
    slider = QSlider(Qt.Orientation.Horizontal)
    slider.setRange(0, 100)
    slider.setSingleStep(1)
    slider.setPageStep(5)
    slider.setValue(int(max(0, min(100, int(task_config.get(config_key, 100))))))
    value_label = QLabel(f"{slider.value()}%")
    value_label.setMinimumWidth(40)

    def on_slider_changed(v: int) -> None:
      clamped = int(max(0, min(100, v)))
      task_config.update({config_key: clamped})
      value_label.setText(f"{clamped}%")

    slider.valueChanged.connect(on_slider_changed)
    row_layout.addWidget(slider, 1)
    row_layout.addWidget(value_label)
    return row

  direction_group = QGroupBox("Direction Influence")
  direction_group_layout = QVBoxLayout(direction_group)
  direction_group_layout.addWidget(make_direction_slider_row("Up", "up_influence_pct"))
  direction_group_layout.addWidget(make_direction_slider_row("Down", "down_influence_pct"))
  direction_group_layout.addWidget(make_direction_slider_row("Left", "left_influence_pct"))
  direction_group_layout.addWidget(make_direction_slider_row("Right", "right_influence_pct"))
  layout.addWidget(direction_group)

  if "targets" not in task_config or not task_config["targets"]:
    task_config["targets"] = [
      {
        "name": "Target 1",
        "enabled": True,
        "x_norm": 0.75,
        "y_norm": 0.50,
        "radius_ratio": DEFAULT_TARGET_RADIUS_RATIO,
        "hold_time": DEFAULT_TARGET_HOLD_TIME,
        "reward_channel": int(task_config.get("reward_channel", 0)),
        "target_color": DEFAULT_TARGET_COLOR.copy(),
        "target_opacity": DEFAULT_TARGET_OPACITY,
        "target_active_color": DEFAULT_TARGET_ACTIVE_COLOR.copy(),
        "target_active_opacity": DEFAULT_TARGET_ACTIVE_OPACITY,
      }
    ]

  targets = task_config["targets"]
  target_table = QTableWidget()
  target_table.setColumnCount(11)
  target_table.setHorizontalHeaderLabels([
    "Enabled",
    "Name",
    "X",
    "Y",
    "Radius",
    "Hold (s)",
    "Reward Channel",
    "Static Color",
    "Static Opacity",
    "Active Color",
    "Active Opacity",
  ])
  target_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
  target_table.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
  target_table.horizontalHeader().setStretchLastSection(True)
  target_table.verticalHeader().setVisible(False)
  target_table.setMinimumHeight(190)
  target_table.setToolTip("Each row is a target. Multi-select rows to remove several at once.")

  def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))

  def normalize_target(target: typing.Any) -> typing.Dict[str, typing.Any]:
    t = dict(target) if isinstance(target, dict) else {}
    color = normalize_rgb(t.get("target_color", DEFAULT_TARGET_COLOR), DEFAULT_TARGET_COLOR)
    active_color = normalize_rgb(t.get("target_active_color", DEFAULT_TARGET_ACTIVE_COLOR), DEFAULT_TARGET_ACTIVE_COLOR)
    return {
      "name": str(t.get("name", "")),
      "enabled": bool(t.get("enabled", True)),
      "group": str(t.get("group", "")),
      "x_norm": clamp(float(t.get("x_norm", 0.75)), 0.0, 1.0),
      "y_norm": clamp(float(t.get("y_norm", 0.50)), 0.0, 1.0),
      "radius_ratio": clamp(float(t.get("radius_ratio", DEFAULT_TARGET_RADIUS_RATIO)), 0.01, 0.5),
      "hold_time": clamp(float(t.get("hold_time", DEFAULT_TARGET_HOLD_TIME)), 0.01, 10.0),
      "reward_channel": max(0, int(t.get("reward_channel", task_config.get("reward_channel", 0)))),
      "target_color": color,
      "target_opacity": clamp_float(t.get("target_opacity", DEFAULT_TARGET_OPACITY), 0.0, 1.0, DEFAULT_TARGET_OPACITY),
      "target_active_color": active_color,
      "target_active_opacity": clamp_float(
        t.get("target_active_opacity", DEFAULT_TARGET_ACTIVE_OPACITY),
        0.0,
        1.0,
        DEFAULT_TARGET_ACTIVE_OPACITY,
      ),
    }

  def normalize_schedule(raw: typing.Any) -> typing.Dict[str, typing.Any]:
    """Normalize a target_schedule dict to the schema the Rust executor parses.

    Scheduling is honored by the Rust executor only (joystick_intro_rust task);
    the pure-Python run() below intentionally stays random-only.
    """
    if isinstance(raw, ObservableCollection):
      raw = raw.unwrap()
    r = dict(raw) if isinstance(raw, dict) else {}
    mode = str(r.get("mode", "random"))
    if mode not in ("random", "sequence", "center_out"):
      mode = "random"
    order = r.get("order", [])
    peripherals = r.get("peripherals", [])
    peripheral_order = str(r.get("peripheral_order", "sequential"))
    if peripheral_order not in ("sequential", "random"):
      peripheral_order = "sequential"
    return {
      "mode": mode,
      "order": [str(name) for name in order] if isinstance(order, list) else [],
      "center": str(r.get("center", "")),
      "peripherals": [str(name) for name in peripherals] if isinstance(peripherals, list) else [],
      "peripheral_order": peripheral_order,
      "interleave_random_ratio": clamp_float(r.get("interleave_random_ratio", 0.0), 0.0, 1.0, 0.0),
    }

  def make_default_target(index: int) -> typing.Dict[str, typing.Any]:
    return {
      "name": f"Target {index}",
      "enabled": True,
      "x_norm": 0.75,
      "y_norm": 0.50,
      "radius_ratio": DEFAULT_TARGET_RADIUS_RATIO,
      "hold_time": DEFAULT_TARGET_HOLD_TIME,
      "reward_channel": int(task_config.get("reward_channel", 0)),
      "target_color": DEFAULT_TARGET_COLOR.copy(),
      "target_opacity": DEFAULT_TARGET_OPACITY,
      "target_active_color": DEFAULT_TARGET_ACTIVE_COLOR.copy(),
      "target_active_opacity": DEFAULT_TARGET_ACTIVE_OPACITY,
    }

  GROUPLESS_LABEL = "(ungrouped)"

  def target_group_key(target: typing.Any) -> str:
    group = str(target.get("group", "")) if isinstance(target, dict) else ""
    return group or GROUPLESS_LABEL

  def rebuild_group_toggle_rows(
    container: QWidget,
    container_layout: typing.Any,
    targets_list: typing.Iterable[typing.Dict[str, typing.Any]],
    on_set_group_enabled: typing.Callable[[str, bool], None],
  ) -> None:
    """Rebuild one-click enable/disable checkboxes, one per target group.

    Groups come from the per-target "group" field (stamped by preset Load/
    Append). The checkbox is checked only when every target in the group is
    enabled; clicking it enables/disables the whole group. The panel hides
    itself while no target carries a group, since Enable/Disable All already
    covers the ungrouped-only case.
    """
    while container_layout.count():
      item = container_layout.takeAt(0)
      widget = item.widget()
      if widget is not None:
        widget.setParent(None)
        widget.deleteLater()
    groups: typing.Dict[str, typing.List[bool]] = {}
    for target in targets_list:
      groups.setdefault(target_group_key(target), []).append(
        bool(target.get("enabled", True)) if isinstance(target, dict) else True
      )
    has_groups = any(key != GROUPLESS_LABEL for key in groups)
    container.setVisible(has_groups)
    if not has_groups:
      return
    for key in sorted(groups):
      flags = groups[key]
      on_count = sum(1 for flag in flags if flag)
      box = QCheckBox(f"{key} ({on_count}/{len(flags)} on)")
      box.setToolTip(
        f"Enable or disable every target in group '{key}' at once. "
        "Checked = all enabled; unchecked = some or all disabled."
      )
      box.blockSignals(True)
      box.setChecked(on_count == len(flags))
      box.blockSignals(False)
      box.clicked.connect(lambda checked, group=key: on_set_group_enabled(group, bool(checked)))
      container_layout.addWidget(box)

  def make_target_from_template(
    template_target: typing.Dict[str, typing.Any],
    index: int,
    x_norm: float,
    y_norm: float,
    prefix: str,
  ) -> typing.Dict[str, typing.Any]:
    base = normalize_target(template_target)
    base["name"] = f"{prefix.strip() or 'Target'} {index}"
    base["x_norm"] = clamp(float(x_norm), 0.0, 1.0)
    base["y_norm"] = clamp(float(y_norm), 0.0, 1.0)
    return base

  def generate_rectangular_grid_points(
    rows: int,
    columns: int,
    edge_margin: float,
    center_exclusion_radius: float,
  ) -> typing.List[typing.Tuple[float, float]]:
    points: typing.List[typing.Tuple[float, float]] = []
    rows = max(1, int(rows))
    columns = max(1, int(columns))
    margin = clamp(float(edge_margin), 0.0, 0.45)
    exclusion_radius = max(0.0, float(center_exclusion_radius))
    usable_left = margin
    usable_right = 1.0 - margin
    usable_bottom = margin
    usable_top = 1.0 - margin
    if usable_right <= usable_left or usable_top <= usable_bottom:
      return points

    def axis_positions(count: int, lo: float, hi: float) -> typing.List[float]:
      if count <= 1:
        return [(lo + hi) / 2.0]
      step = (hi - lo) / float(count - 1)
      return [lo + i * step for i in range(count)]

    x_positions = axis_positions(columns, usable_left, usable_right)
    y_positions = axis_positions(rows, usable_bottom, usable_top)
    for y in y_positions:
      for x in x_positions:
        if math.hypot(x - 0.5, y - 0.5) < exclusion_radius:
          continue
        points.append((x, y))
    return points

  def generate_hexagonal_points(
    rows: int,
    columns: int,
    edge_margin: float,
    center_exclusion_radius: float,
  ) -> typing.List[typing.Tuple[float, float]]:
    points: typing.List[typing.Tuple[float, float]] = []
    rows = max(1, int(rows))
    columns = max(1, int(columns))
    margin = clamp(float(edge_margin), 0.0, 0.45)
    exclusion_radius = max(0.0, float(center_exclusion_radius))
    usable_left = margin
    usable_right = 1.0 - margin
    usable_bottom = margin
    usable_top = 1.0 - margin
    if usable_right <= usable_left or usable_top <= usable_bottom:
      return points

    width = usable_right - usable_left
    height = usable_top - usable_bottom
    x_step = 0.0 if columns <= 1 else width / float(columns - 1)
    y_step = 0.0 if rows <= 1 else height / float(rows - 1)
    if rows > 1 and columns > 1 and y_step <= 0.0:
      y_step = x_step * math.sqrt(3.0) / 2.0

    for row in range(rows):
      row_y = usable_bottom + (height / 2.0 if rows <= 1 else row * y_step)
      if row_y < usable_bottom - 1e-9 or row_y > usable_top + 1e-9:
        continue
      offset = 0.5 * x_step if (row % 2 == 1 and columns > 1) else 0.0
      for column in range(columns):
        row_x = usable_left + (width / 2.0 if columns <= 1 else column * x_step + offset)
        if row_x < usable_left - 1e-9 or row_x > usable_right + 1e-9:
          continue
        if math.hypot(row_x - 0.5, row_y - 0.5) < exclusion_radius:
          continue
        points.append((row_x, row_y))
    return points

  def generate_annulus_points(
    ring_count: int,
    points_per_ring: int,
    inner_radius: float,
    outer_radius: float,
    angle_offset_deg: float,
  ) -> typing.List[typing.Tuple[float, float]]:
    points: typing.List[typing.Tuple[float, float]] = []
    ring_count = max(1, int(ring_count))
    points_per_ring = max(1, int(points_per_ring))
    inner = clamp(float(inner_radius), 0.0, 0.70)
    outer = clamp(float(outer_radius), inner, 0.70)
    angle_offset = math.radians(float(angle_offset_deg))

    radii = [outer] if ring_count <= 1 else [
      inner + (outer - inner) * (i / float(ring_count - 1))
      for i in range(ring_count)
    ]
    for ring_index, radius in enumerate(radii):
      ring_points = max(1, points_per_ring * (ring_index + 1))
      for point_index in range(ring_points):
        angle = angle_offset + (2.0 * math.pi * point_index / float(ring_points))
        x = 0.5 + radius * math.cos(angle)
        y = 0.5 + radius * math.sin(angle)
        if x < 0.0 or x > 1.0 or y < 0.0 or y > 1.0:
          continue
        points.append((x, y))
    return points

  class LayoutPreview(QWidget):
    def __init__(
      self,
      targets_ref: typing.List[typing.Dict[str, typing.Any]],
      generated_preview_getter: typing.Callable[[], typing.List[typing.Dict[str, typing.Any]]],
      cursor_radius_getter: typing.Callable[[], float],
      select_callback: typing.Callable[[int], None],
      update_callback: typing.Callable[[], None],
      parent: typing.Optional[QWidget] = None,
    ) -> None:
      super().__init__(parent)
      self.targets_ref = targets_ref
      self.generated_preview_getter = generated_preview_getter
      self.cursor_radius_getter = cursor_radius_getter
      self.select_callback = select_callback
      self.update_callback = update_callback
      self.selected_index = -1
      self.hovered_index = -1
      self.drag_index = -1
      self.drag_enabled = True
      self.setMinimumSize(360, 320)
      self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
      self.setMouseTracking(True)

    def set_selected_index(self, index: int) -> None:
      self.selected_index = index
      self.update()

    def set_drag_enabled(self, enabled: bool) -> None:
      self.drag_enabled = bool(enabled)
      if not self.drag_enabled:
        self.drag_index = -1
      self.update()

    def _region_rect(self) -> QRectF:
      w = float(max(1, self.width()))
      h = float(max(1, self.height()))
      margin = 14.0
      avail_w = max(1.0, w - 2.0 * margin)
      avail_h = max(1.0, h - 2.0 * margin)
      cfg_w = max(0.05, min(1.0, float(task_config.get("task_region_width", 0.5))))
      cfg_h = max(0.05, min(1.0, float(task_config.get("task_region_height", 0.67))))
      screen_aspect = 1.0
      screen = self.screen() if hasattr(self, "screen") else None
      if screen is not None:
        screen_geometry = screen.geometry()
        screen_h = max(1, int(screen_geometry.height()))
        screen_aspect = max(0.1, min(10.0, float(screen_geometry.width()) / float(screen_h)))
      else:
        try:
          screen_geometry = qt_screen_geometry()
          screen_h = max(1, int(screen_geometry.height()))
          screen_aspect = max(0.1, min(10.0, float(screen_geometry.width()) / float(screen_h)))
        except Exception:
          screen_aspect = 1.0
      canvas_w = cfg_w * screen_aspect
      canvas_h = cfg_h
      scale = min(avail_w / canvas_w, avail_h / canvas_h)
      region_w = canvas_w * scale
      region_h = canvas_h * scale
      left = (w - region_w) / 2.0
      top = (h - region_h) / 2.0
      return QRectF(left, top, region_w, region_h)

    def _target_center(self, rect: QRectF, target: typing.Dict[str, typing.Any]) -> QPointF:
      x_norm = max(0.0, min(1.0, float(target.get("x_norm", 0.75))))
      y_norm = max(0.0, min(1.0, float(target.get("y_norm", 0.50))))
      x = rect.left() + x_norm * rect.width()
      y = rect.top() + (1.0 - y_norm) * rect.height()
      return QPointF(x, y)

    def _target_radius_px(self, rect: QRectF, target: typing.Dict[str, typing.Any]) -> float:
      ratio = max(0.01, min(0.5, float(target.get("radius_ratio", DEFAULT_TARGET_RADIUS_RATIO))))
      return ratio * min(rect.width(), rect.height())

    def _event_pos(self, event: typing.Any) -> QPointF:
      if hasattr(event, "position"):
        return event.position()
      if hasattr(event, "localPos"):
        return event.localPos()
      return QPointF(float(event.x()), float(event.y()))

    def _hit_test(self, pos: QPointF) -> int:
      rect = self._region_rect()
      best_index = -1
      best_distance = None
      for i, target in enumerate(self.targets_ref):
        center = self._target_center(rect, target)
        radius = self._target_radius_px(rect, target)
        dist = math.hypot(pos.x() - center.x(), pos.y() - center.y())
        if dist <= radius + 6.0:
          if best_distance is None or dist < best_distance:
            best_index = i
            best_distance = dist
      return best_index

    def _move_target_to_pos(self, index: int, pos: QPointF) -> None:
      if index < 0 or index >= len(self.targets_ref):
        return
      rect = self._region_rect()
      if rect.width() <= 0.0 or rect.height() <= 0.0:
        return
      clamped_x = max(rect.left(), min(rect.right(), pos.x()))
      clamped_y = max(rect.top(), min(rect.bottom(), pos.y()))
      x_norm = (clamped_x - rect.left()) / max(1.0, rect.width())
      y_norm = 1.0 - ((clamped_y - rect.top()) / max(1.0, rect.height()))
      self.targets_ref[index]["x_norm"] = max(0.0, min(1.0, x_norm))
      self.targets_ref[index]["y_norm"] = max(0.0, min(1.0, y_norm))
      self.update_callback()
      self.update()

    def mousePressEvent(self, event: typing.Any) -> None:
      if event.button() != Qt.MouseButton.LeftButton:
        return super().mousePressEvent(event)
      index = self._hit_test(self._event_pos(event))
      self.drag_index = index if self.drag_enabled else -1
      self.selected_index = index
      self.select_callback(index)
      self.update()

    def mouseMoveEvent(self, event: typing.Any) -> None:
      if self.drag_index >= 0:
        self._move_target_to_pos(self.drag_index, self._event_pos(event))
        return
      hovered = self._hit_test(self._event_pos(event))
      if hovered != self.hovered_index:
        self.hovered_index = hovered
        self.update()
      super().mouseMoveEvent(event)

    def leaveEvent(self, event: typing.Any) -> None:
      if self.hovered_index != -1:
        self.hovered_index = -1
        self.update()
      super().leaveEvent(event)

    def mouseReleaseEvent(self, event: typing.Any) -> None:
      if event.button() == Qt.MouseButton.LeftButton:
        self.drag_index = -1
      super().mouseReleaseEvent(event)

    def _paint_frame(self, painter: QPainter) -> QRectF:
      """Background, task-region rect, and grid shared by both preview tabs."""
      painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
      painter.fillRect(self.rect(), QColor(24, 24, 24))
      region_rect = self._region_rect()
      painter.setPen(QPen(QColor(90, 90, 90), 1))
      painter.setBrush(QColor(40, 40, 40))
      painter.drawRect(region_rect)

      grid_pen = QPen(QColor(70, 70, 70), 1, Qt.PenStyle.DotLine)
      painter.setPen(grid_pen)
      for i in range(1, 4):
        x = region_rect.left() + region_rect.width() * (i / 4.0)
        y = region_rect.top() + region_rect.height() * (i / 4.0)
        painter.drawLine(int(x), int(region_rect.top()), int(x), int(region_rect.bottom()))
        painter.drawLine(int(region_rect.left()), int(y), int(region_rect.right()), int(y))
      return region_rect

    def paintEvent(self, event: typing.Any) -> None:
      painter = QPainter(self)
      region_rect = self._paint_frame(painter)

      # Targets are drawn as hollow rings (never filled) so overlapping targets
      # remain individually distinguishable. Enabled targets use a solid stroke
      # in the target color; disabled targets use a dimmer dashed stroke.
      for i, target in enumerate(self.targets_ref):
        center = self._target_center(region_rect, target)
        radius = self._target_radius_px(region_rect, target)
        rgb = target.get("target_color", DEFAULT_TARGET_COLOR)
        is_selected = i == self.selected_index
        is_enabled = bool(target.get("enabled", True))
        if is_enabled:
          ring_color = QColor(int(rgb[0]), int(rgb[1]), int(rgb[2]))
          ring_pen = QPen(QColor(255, 255, 255) if is_selected else ring_color, 2, Qt.PenStyle.SolidLine)
        else:
          # Greyed, dashed, thinner ring for disabled targets.
          ring_color = QColor(150, 150, 150)
          ring_pen = QPen(QColor(255, 255, 255) if is_selected else ring_color, 1, Qt.PenStyle.DashLine)
        painter.setPen(ring_pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(center, radius, radius)
        if is_selected:
          painter.setPen(QPen(QColor(255, 255, 255), 2, Qt.PenStyle.DashLine))
          painter.setBrush(Qt.BrushStyle.NoBrush)
          painter.drawEllipse(center, radius + 6.0, radius + 6.0)
          active_rgb = target.get("target_active_color", DEFAULT_TARGET_ACTIVE_COLOR)
          active_color = color_with_opacity(active_rgb, float(target.get("target_active_opacity", DEFAULT_TARGET_ACTIVE_OPACITY)))
          active_radius = max(5.0, min(14.0, radius * 0.42))
          active_center = QPointF(center.x() - radius * 0.35, center.y() - radius * 0.35)
          painter.setPen(QPen(QColor(255, 255, 255), 1))
          painter.setBrush(active_color)
          painter.drawEllipse(active_center, active_radius, active_radius)
        # Only label the selected and hovered targets to avoid text pile-up.
        if is_selected or i == self.hovered_index:
          painter.setPen(QPen(QColor(255, 255, 255), 1))
          label = str(target.get("name", f"T{i + 1}"))
          painter.drawText(int(center.x() + radius + 6.0), int(center.y() - radius - 4.0), label)

      preview_pen = QPen(QColor(120, 200, 255, 220), 2, Qt.PenStyle.DashLine)
      for i, target in enumerate(self.generated_preview_getter()):
        center = self._target_center(region_rect, target)
        radius = self._target_radius_px(region_rect, target)
        painter.setPen(preview_pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(center, radius, radius)

      cursor_center = QPointF(region_rect.center().x(), region_rect.center().y())
      cursor_radius = max(0.005, min(0.5, float(self.cursor_radius_getter()))) * min(region_rect.width(), region_rect.height())
      cursor_color_rgb = task_config.get("cursor_color", [255, 70, 70])
      cursor_color = QColor(int(cursor_color_rgb[0]), int(cursor_color_rgb[1]), int(cursor_color_rgb[2]))
      painter.setPen(QPen(QColor(255, 255, 255), 1))
      painter.setBrush(cursor_color)
      painter.drawEllipse(cursor_center, cursor_radius, cursor_radius)

      painter.setPen(QPen(QColor(200, 200, 200), 1))
      if self.drag_enabled:
        painter.drawText(12, 18, "Drag target centers to reposition them. Hover a ring to see its name.")
      else:
        painter.drawText(12, 18, "Drag move is disabled. Click or hover targets to identify them.")
      painter.drawText(12, 36, "Solid rings = enabled, dashed grey rings = disabled.")
      if self.generated_preview_getter():
        painter.drawText(12, 54, "Dashed blue rings are generator preview targets and are not saved yet.")

  class SchedulePreview(LayoutPreview):
    """Structured-schedule view (the editor's second preview tab).

    Renders ONLY the schedule — sequence order or center/ring roles, with
    always-on name labels — so structured targets cannot be confused with the
    random-eligible layout shown in the Layout tab. Clicking still selects the
    target (selection is shared with the Layout tab); dragging is disabled.
    """

    def __init__(
      self,
      targets_ref: typing.List[typing.Dict[str, typing.Any]],
      schedule_getter: typing.Callable[[], typing.Dict[str, typing.Any]],
      cursor_radius_getter: typing.Callable[[], float],
      select_callback: typing.Callable[[int], None],
      update_callback: typing.Callable[[], None],
      parent: typing.Optional[QWidget] = None,
    ) -> None:
      super().__init__(targets_ref, lambda: [], cursor_radius_getter, select_callback, update_callback, parent)
      self.schedule_getter = schedule_getter
      self.set_drag_enabled(False)

    def _target_ring_pen(self, target: typing.Dict[str, typing.Any], alpha: int = 255, width: int = 2) -> QPen:
      rgb = target.get("target_color", DEFAULT_TARGET_COLOR)
      return QPen(QColor(int(rgb[0]), int(rgb[1]), int(rgb[2]), alpha), width)

    def _draw_label(self, painter: QPainter, center: QPointF, radius: float, text: str) -> None:
      painter.setPen(QPen(QColor(235, 235, 235), 1))
      painter.drawText(int(center.x() + radius + 6.0), int(center.y() - radius - 4.0), text)

    def _draw_badge(self, painter: QPainter, center: QPointF, radius: float, text: str, fill: QColor) -> None:
      badge_radius = 9.0
      badge_center = QPointF(center.x() - radius * 0.7, center.y() - radius * 0.7)
      painter.save()
      painter.setPen(QPen(QColor(255, 255, 255), 1))
      painter.setBrush(fill)
      painter.drawEllipse(badge_center, badge_radius, badge_radius)
      badge_font = QFont(painter.font())
      badge_font.setBold(True)
      badge_font.setPointSize(9)
      painter.setFont(badge_font)
      painter.drawText(
        QRectF(badge_center.x() - badge_radius, badge_center.y() - badge_radius, badge_radius * 2.0, badge_radius * 2.0),
        Qt.AlignmentFlag.AlignCenter,
        text,
      )
      painter.restore()

    def _draw_arrow(self, painter: QPainter, start: QPointF, end: QPointF, start_radius: float, end_radius: float) -> None:
      dx = end.x() - start.x()
      dy = end.y() - start.y()
      length = math.hypot(dx, dy)
      if length <= start_radius + end_radius + 10.0:
        return
      ux, uy = dx / length, dy / length
      p1 = QPointF(start.x() + ux * (start_radius + 4.0), start.y() + uy * (start_radius + 4.0))
      p2 = QPointF(end.x() - ux * (end_radius + 4.0), end.y() - uy * (end_radius + 4.0))
      painter.drawLine(QLineF(p1, p2))
      head = 7.0
      left = QPointF(p2.x() - ux * head - uy * head * 0.5, p2.y() - uy * head + ux * head * 0.5)
      right = QPointF(p2.x() - ux * head + uy * head * 0.5, p2.y() - uy * head - ux * head * 0.5)
      painter.drawLine(QLineF(p2, left))
      painter.drawLine(QLineF(p2, right))

    def paintEvent(self, event: typing.Any) -> None:
      painter = QPainter(self)
      region_rect = self._paint_frame(painter)
      schedule = self.schedule_getter()
      mode = str(schedule.get("mode", "random"))

      def target_name(target: typing.Dict[str, typing.Any]) -> str:
        return str(target.get("name", ""))

      def is_enabled(target: typing.Dict[str, typing.Any]) -> bool:
        return bool(target.get("enabled", True))

      def draw_faint(target: typing.Dict[str, typing.Any]) -> None:
        center = self._target_center(region_rect, target)
        radius = self._target_radius_px(region_rect, target)
        style = Qt.PenStyle.SolidLine if is_enabled(target) else Qt.PenStyle.DashLine
        painter.setPen(QPen(QColor(150, 150, 150, 70), 1, style))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(center, radius, radius)

      def draw_selection_halo(index: int, center: QPointF, radius: float) -> None:
        if index == self.selected_index:
          painter.setPen(QPen(QColor(255, 255, 255), 2, Qt.PenStyle.DashLine))
          painter.setBrush(Qt.BrushStyle.NoBrush)
          painter.drawEllipse(center, radius + 6.0, radius + 6.0)

      badge_blue = QColor(60, 120, 220)
      missing: typing.List[str] = []

      if mode == "sequence":
        caption = "Fixed Sequence — numbered targets cycle in list order (reorder with Move Up/Down)."
        participants = [(i, t) for i, t in enumerate(self.targets_ref) if is_enabled(t)]
        for target in self.targets_ref:
          if not is_enabled(target):
            draw_faint(target)
        if len(participants) >= 2:
          painter.setPen(QPen(QColor(200, 200, 200, 130), 1))
          for k, (i1, t1) in enumerate(participants):
            i2, t2 = participants[(k + 1) % len(participants)]
            if i1 == i2:
              continue
            self._draw_arrow(
              painter,
              self._target_center(region_rect, t1),
              self._target_center(region_rect, t2),
              self._target_radius_px(region_rect, t1),
              self._target_radius_px(region_rect, t2),
            )
        for order_pos, (i, target) in enumerate(participants):
          center = self._target_center(region_rect, target)
          radius = self._target_radius_px(region_rect, target)
          painter.setPen(self._target_ring_pen(target))
          painter.setBrush(Qt.BrushStyle.NoBrush)
          painter.drawEllipse(center, radius, radius)
          draw_selection_halo(i, center, radius)
          self._draw_badge(painter, center, radius, str(order_pos + 1), badge_blue)
          self._draw_label(painter, center, radius, target_name(target).strip() or f"T{i + 1}")
      elif mode == "center_out":
        caption = "Center-Out — C alternates with the numbered ring targets. Faint targets are not in the schedule."
        center_name = str(schedule.get("center", ""))
        ring_names = [str(name) for name in schedule.get("peripherals", [])]
        if not ring_names:
          ring_names = [
            target_name(t) for t in self.targets_ref
            if is_enabled(t) and target_name(t).strip() and target_name(t) != center_name
          ]
        by_name: typing.Dict[str, typing.Tuple[int, typing.Dict[str, typing.Any]]] = {}
        for i, target in enumerate(self.targets_ref):
          by_name.setdefault(target_name(target), (i, target))
        ring_set = set(ring_names)
        for target in self.targets_ref:
          name = target_name(target)
          if (name != center_name and name not in ring_set) or not is_enabled(target):
            draw_faint(target)
        if center_name.strip() and center_name in by_name and is_enabled(by_name[center_name][1]):
          i, target = by_name[center_name]
          center = self._target_center(region_rect, target)
          radius = self._target_radius_px(region_rect, target)
          painter.setBrush(Qt.BrushStyle.NoBrush)
          painter.setPen(QPen(QColor(255, 255, 255), 3))
          painter.drawEllipse(center, radius, radius)
          painter.setPen(self._target_ring_pen(target))
          painter.drawEllipse(center, max(1.0, radius - 4.0), max(1.0, radius - 4.0))
          draw_selection_halo(i, center, radius)
          self._draw_badge(painter, center, radius, "C", QColor(230, 180, 40))
          self._draw_label(painter, center, radius, f"C: {center_name.strip()}")
        else:
          missing.append(center_name.strip() or "(no center)")
        random_ring = str(schedule.get("peripheral_order", "sequential")) == "random"
        for visit_pos, name in enumerate(ring_names):
          entry = by_name.get(name)
          if entry is None or not is_enabled(entry[1]):
            if name not in missing:
              missing.append(name)
            continue
          i, target = entry
          center = self._target_center(region_rect, target)
          radius = self._target_radius_px(region_rect, target)
          painter.setPen(self._target_ring_pen(target))
          painter.setBrush(Qt.BrushStyle.NoBrush)
          painter.drawEllipse(center, radius, radius)
          draw_selection_halo(i, center, radius)
          self._draw_badge(painter, center, radius, "?" if random_ring else str(visit_pos + 1), badge_blue)
          self._draw_label(painter, center, radius, name.strip() or f"T{i + 1}")
      else:
        caption = "Random mode — every enabled target is equally likely; no structured pattern."
        for i, target in enumerate(self.targets_ref):
          if not is_enabled(target):
            draw_faint(target)
            continue
          center = self._target_center(region_rect, target)
          radius = self._target_radius_px(region_rect, target)
          painter.setPen(self._target_ring_pen(target, alpha=90, width=1))
          painter.setBrush(Qt.BrushStyle.NoBrush)
          painter.drawEllipse(center, radius, radius)
          draw_selection_halo(i, center, radius)
          if i in (self.selected_index, self.hovered_index):
            self._draw_label(painter, center, radius, target_name(target).strip() or f"T{i + 1}")

      painter.setPen(QPen(QColor(200, 200, 200), 1))
      painter.drawText(12, 18, caption)
      if missing:
        painter.setPen(QPen(QColor(255, 110, 110), 1))
        painter.drawText(
          12, self.height() - 26,
          "Unavailable: " + ", ".join(missing) + " — these slots fall back to random draws.",
        )
      painter.setPen(QPen(QColor(200, 200, 200), 1))
      painter.drawText(12, self.height() - 10, schedule_pattern_preview(schedule, list(self.targets_ref)))

  class PersistentLayoutEditorDialog(QDialog):
    def __init__(
      self,
      config: ObservableCollection,
      config_key: str,
      parent: typing.Optional[QWidget] = None,
    ) -> None:
      super().__init__(parent, Qt.WindowType.Window)
      self._config = config
      self._config_key = config_key
      self._persist_enabled = False

    def restore_persisted_geometry(self, fallback_width: int, fallback_height: int) -> None:
      raw = self._config.get(self._config_key, {})
      geometry = dict(raw) if isinstance(raw, dict) else {}
      width = max(320, int(geometry.get("width", fallback_width)))
      height = max(240, int(geometry.get("height", fallback_height)))
      x_value = geometry.get("x", None)
      y_value = geometry.get("y", None)
      self.resize(width, height)
      if x_value is not None and y_value is not None:
        self.move(int(x_value), int(y_value))
      self._persist_enabled = True
      self._save_geometry()

    def _save_geometry(self) -> None:
      if not self._persist_enabled or self.isMinimized() or self.isMaximized():
        return
      pos = self.pos()
      size = self.size()
      self._config[self._config_key] = {
        "x": int(pos.x()),
        "y": int(pos.y()),
        "width": int(size.width()),
        "height": int(size.height()),
      }

    def moveEvent(self, event: typing.Any) -> None:
      super().moveEvent(event)
      self._save_geometry()

    def resizeEvent(self, event: typing.Any) -> None:
      super().resizeEvent(event)
      self._save_geometry()

    def closeEvent(self, event: typing.Any) -> None:
      self._save_geometry()
      super().closeEvent(event)

  def open_layout_editor() -> None:
    existing_dialog = getattr(result, "_layout_editor_dialog", None)
    if isinstance(existing_dialog, QDialog):
      existing_dialog.show()
      existing_dialog.raise_()
      existing_dialog.activateWindow()
      return

    dialog = PersistentLayoutEditorDialog(task_config, "target_layout_editor_geometry", result)
    dialog.setWindowTitle("Target Layout Editor")
    dialog.setModal(False)
    dialog.setWindowModality(Qt.WindowModality.NonModal)
    dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose, True)
    dialog.restore_persisted_geometry(900, 560)
    result._layout_editor_dialog = dialog # type: ignore[attr-defined]

    def clear_layout_editor_reference(*_args: typing.Any) -> None:
      if getattr(result, "_layout_editor_dialog", None) is dialog:
        result._layout_editor_dialog = None # type: ignore[attr-defined]

    dialog.destroyed.connect(clear_layout_editor_reference)

    draft_targets = [normalize_target(target) for target in list(targets)]
    draft_schedule = normalize_schedule(task_config.get("target_schedule", {}))
    pending_generated_targets: typing.List[typing.Dict[str, typing.Any]] = []
    pending_generator_operation = "append"
    # An empty peripherals list normally means Auto (the runtime rule). This
    # flag lets the EDITOR sit on "explicit but nothing checked yet" after
    # Uncheck All, so the operator can build a ring from scratch instead of
    # pruning a re-seeded full ring. Saving while empty still runs as Auto
    # (the hint label says so).
    ring_explicit_when_empty = False

    dialog_layout = QHBoxLayout(dialog)
    dialog_layout.setContentsMargins(8, 8, 8, 8)
    dialog_layout.setSpacing(8)
    preview: typing.Optional[LayoutPreview] = None
    schedule_preview: typing.Optional[SchedulePreview] = None
    selected_index = 0 if draft_targets else -1
    draft_cursor_radius = max(0.005, min(0.5, float(task_config.get("cursor_diameter_ratio", 0.1)) / 2.0))

    side_panel = QWidget(dialog)
    side_layout = QVBoxLayout(side_panel)
    side_layout.setContentsMargins(0, 0, 0, 0)
    side_layout.setSpacing(5)
    side_panel.setMinimumWidth(260)
    side_panel.setMaximumWidth(340)
    compact_font = QFont(side_panel.font())
    if compact_font.pointSize() > 0:
      compact_font.setPointSize(max(9, compact_font.pointSize() - 1))
    side_panel.setFont(compact_font)
    side_panel.setStyleSheet(
      "QWidget { font-size: 11px; }"
      "QGroupBox { margin-top: 8px; padding-top: 8px; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 6px; padding: 0 2px; }"
      "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { min-height: 18px; max-height: 22px; }"
      "QPushButton { min-height: 20px; padding: 1px 5px; }"
      "QCheckBox { spacing: 4px; }"
    )

    target_list = QListWidget()
    target_list.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    target_list.setToolTip("Select one or more targets. Drag in the preview to reposition the current target.")
    target_list.setMaximumHeight(120)
    side_layout.addWidget(QLabel("Targets"))
    side_layout.addWidget(target_list)

    preset_combo = QComboBox()
    preset_combo.setToolTip("Saved target layouts. Load restores the whole draft from the chosen preset.")
    preset_save_button = QPushButton("Save")
    preset_load_button = QPushButton("Load")
    preset_append_button = QPushButton("Append")
    preset_delete_button = QPushButton("Delete")
    preset_save_button.setToolTip("Save the current draft targets as a named preset.")
    preset_load_button.setToolTip("Replace the draft with the selected preset.")
    preset_append_button.setToolTip(
      "Add the selected preset's targets to the current draft (nothing is replaced). "
      "The draft schedule is kept unless you opt to adopt the preset's schedule."
    )
    preset_delete_button.setToolTip("Delete the selected preset.")
    preset_row = QWidget()
    preset_row_layout = QHBoxLayout(preset_row)
    preset_row_layout.setContentsMargins(0, 0, 0, 0)
    preset_row_layout.addWidget(preset_combo, 1)
    preset_row_layout.addWidget(preset_save_button)
    preset_row_layout.addWidget(preset_load_button)
    preset_row_layout.addWidget(preset_append_button)
    preset_row_layout.addWidget(preset_delete_button)
    side_layout.addWidget(QLabel("Presets"))
    side_layout.addWidget(preset_row)

    group_panel_box = QGroupBox("Groups")
    group_panel_box.setToolTip(
      "One checkbox per preset group. Click to enable/disable every target that "
      "was loaded or appended from that preset."
    )
    group_panel_layout = QVBoxLayout(group_panel_box)
    group_panel_layout.setContentsMargins(6, 4, 6, 4)
    group_panel_layout.setSpacing(2)
    group_panel_box.setVisible(False)
    side_layout.addWidget(group_panel_box)

    # --- Schedule: how the next target is chosen. Honored by the Rust
    # executor only; the pure-Python run() stays random regardless. ---
    schedule_panel = QGroupBox("Schedule")
    schedule_layout = QFormLayout(schedule_panel)
    schedule_layout.setContentsMargins(6, 10, 6, 6)
    schedule_layout.setHorizontalSpacing(6)
    schedule_layout.setVerticalSpacing(3)
    schedule_layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
    schedule_mode_combo = QComboBox()
    schedule_mode_combo.addItem("Random", "random")
    schedule_mode_combo.addItem("Fixed Sequence", "sequence")
    schedule_mode_combo.addItem("Center-Out", "center_out")
    schedule_mode_combo.setToolTip(
      "How the next target is chosen (Rust executor only). Random: uniform over enabled targets. "
      "Fixed Sequence: cycle enabled targets in list order. Center-Out: alternate center and periphery."
    )
    schedule_center_combo = QComboBox()
    schedule_center_combo.setToolTip("Center-Out: which target is the center. Referenced by name.")
    schedule_peripheral_order_combo = QComboBox()
    schedule_peripheral_order_combo.addItem("Sequential", "sequential")
    schedule_peripheral_order_combo.addItem("Random", "random")
    schedule_peripheral_order_combo.setToolTip(
      "Center-Out: cycle the peripheral ring in list order, or draw a random ring target each time."
    )
    schedule_ring_auto_box = QCheckBox("Auto: all enabled non-center")
    schedule_ring_auto_box.setToolTip(
      "Checked: the ring is every enabled non-center target (stored as an empty list). "
      "Uncheck to pick ring members explicitly."
    )
    schedule_ring_list = QListWidget()
    schedule_ring_list.setMaximumHeight(90)
    schedule_ring_list.setToolTip(
      "Center-Out ring members, visited top-to-bottom when Periphery is Sequential. "
      "Greyed items show what Auto resolves to."
    )
    schedule_ring_check_all_button = QPushButton("Check All")
    schedule_ring_uncheck_all_button = QPushButton("Uncheck All")
    schedule_ring_check_all_button.setToolTip(
      "Check every enabled non-center target as an explicit ring member."
    )
    schedule_ring_uncheck_all_button.setToolTip(
      "Uncheck every ring member so you can build the ring from scratch "
      "(check individual targets or a group below)."
    )
    schedule_ring_buttons_row = QWidget()
    schedule_ring_buttons_row_layout = QHBoxLayout(schedule_ring_buttons_row)
    schedule_ring_buttons_row_layout.setContentsMargins(0, 0, 0, 0)
    schedule_ring_buttons_row_layout.addWidget(schedule_ring_check_all_button)
    schedule_ring_buttons_row_layout.addWidget(schedule_ring_uncheck_all_button)
    schedule_ring_buttons_row_layout.addStretch(1)
    schedule_ring_group_row = QWidget()
    schedule_ring_group_row_layout = QHBoxLayout(schedule_ring_group_row)
    schedule_ring_group_row_layout.setContentsMargins(0, 0, 0, 0)
    schedule_ring_group_row_layout.setSpacing(6)
    schedule_ring_group_row.setVisible(False)
    schedule_ring_hint_label = QLabel(
      "Empty ring runs as Auto at runtime — check targets or a group to restrict it."
    )
    schedule_ring_hint_label.setWordWrap(True)
    schedule_ring_hint_label.setStyleSheet("color: rgb(220, 150, 60);")
    schedule_ring_hint_label.setVisible(False)
    schedule_ring_container = QWidget()
    schedule_ring_container_layout = QVBoxLayout(schedule_ring_container)
    schedule_ring_container_layout.setContentsMargins(0, 0, 0, 0)
    schedule_ring_container_layout.setSpacing(2)
    schedule_ring_container_layout.addWidget(schedule_ring_auto_box)
    schedule_ring_container_layout.addWidget(schedule_ring_list)
    schedule_ring_container_layout.addWidget(schedule_ring_buttons_row)
    schedule_ring_container_layout.addWidget(schedule_ring_group_row)
    schedule_ring_container_layout.addWidget(schedule_ring_hint_label)
    schedule_interleave_spin = QDoubleSpinBox()
    schedule_interleave_spin.setRange(0.0, 1.0)
    schedule_interleave_spin.setDecimals(2)
    schedule_interleave_spin.setSingleStep(0.05)
    schedule_interleave_spin.setKeyboardTracking(False)
    schedule_interleave_spin.setToolTip(
      "Probability that a trial is a uniform-random target inserted into the structured stream. "
      "The pattern resumes where it left off after each insertion. In Center-Out, inserts only "
      "land BETWEEN center→peripheral pairs — a pair is never split. "
      "0.00 = pure structured pattern. 1.00 = every trial is random and the structured pattern "
      "NEVER runs — to randomize which peripheral is chosen, set Periphery to 'random' instead."
    )
    schedule_move_up_button = QPushButton("Move Up")
    schedule_move_down_button = QPushButton("Move Down")
    schedule_move_up_button.setToolTip("Move the selected target earlier in the list (sequence order).")
    schedule_move_down_button.setToolTip("Move the selected target later in the list (sequence order).")
    schedule_move_row = QWidget()
    schedule_move_row_layout = QHBoxLayout(schedule_move_row)
    schedule_move_row_layout.setContentsMargins(0, 0, 0, 0)
    schedule_move_row_layout.addWidget(schedule_move_up_button)
    schedule_move_row_layout.addWidget(schedule_move_down_button)
    schedule_layout.addRow("Mode", schedule_mode_combo)
    schedule_layout.addRow("Center", schedule_center_combo)
    schedule_layout.addRow("Periphery", schedule_peripheral_order_combo)
    schedule_layout.addRow("Ring", schedule_ring_container)
    schedule_layout.addRow("Order", schedule_move_row)
    schedule_layout.addRow("Random insert %", schedule_interleave_spin)

    # The settings live in tabs (Target / Generator / Schedule) so no single
    # tab is crammed into a sliver of scroll area. The target list, presets,
    # and Save/Cancel stay visible above/below the tabs at all times.
    no_frame = QFrame.Shape.NoFrame if hasattr(QFrame, "Shape") else QFrame.NoFrame

    def make_settings_tab(*panels: QWidget) -> QScrollArea:
      tab_body = QWidget()
      tab_layout = QVBoxLayout(tab_body)
      tab_layout.setContentsMargins(0, 0, 0, 0)
      tab_layout.setSpacing(5)
      for panel in panels:
        tab_layout.addWidget(panel)
      tab_layout.addStretch(1)
      scroll = QScrollArea(side_panel)
      scroll.setWidgetResizable(True)
      scroll.setFrameShape(no_frame)
      scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
      scroll.setWidget(tab_body)
      return scroll

    settings_tabs = QTabWidget(side_panel)

    form_panel = QGroupBox("Selected Target")
    form_layout = QFormLayout(form_panel)
    form_layout.setContentsMargins(6, 10, 6, 6)
    form_layout.setHorizontalSpacing(6)
    form_layout.setVerticalSpacing(3)
    form_layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
    name_edit = QLineEdit()
    enabled_box = QCheckBox("Enabled")
    x_spin = QDoubleSpinBox()
    y_spin = QDoubleSpinBox()
    radius_spin = QDoubleSpinBox()
    hold_spin = QDoubleSpinBox()
    reward_channel_spin = QSpinBox()
    static_color_button = QPushButton("Choose Static Color")
    static_opacity_spin = QDoubleSpinBox()
    active_color_button = QPushButton("Choose Active Color")
    active_opacity_spin = QDoubleSpinBox()
    enable_drag_box = QCheckBox("Enable drag move")
    enable_drag_box.setChecked(bool(task_config.get("target_layout_editor_drag_enabled", True)))

    for spin in (x_spin, y_spin):
      spin.setRange(0.0, 1.0)
      spin.setDecimals(3)
      spin.setSingleStep(0.01)
    radius_spin.setRange(0.01, 0.5)
    radius_spin.setDecimals(3)
    radius_spin.setSingleStep(0.01)
    hold_spin.setRange(0.01, 10.0)
    hold_spin.setDecimals(3)
    hold_spin.setSingleStep(0.05)
    reward_channel_spin.setRange(0, 255)
    reward_channel_spin.setSingleStep(1)
    for spin in (static_opacity_spin, active_opacity_spin):
      spin.setRange(0.0, 1.0)
      spin.setDecimals(2)
      spin.setSingleStep(0.05)
    for spin in (x_spin, y_spin, radius_spin, hold_spin, reward_channel_spin):
      spin.setKeyboardTracking(False)
    for spin in (static_opacity_spin, active_opacity_spin):
      spin.setKeyboardTracking(False)
    name_edit.setToolTip("Name shown in the target list and operator overlay.")
    enabled_box.setToolTip("Disabled targets stay in the table but are not eligible for trial selection.")
    x_spin.setToolTip("Normalized horizontal position inside the task region. 0 = left, 1 = right.")
    y_spin.setToolTip("Normalized vertical position inside the task region. 0 = bottom, 1 = top.")
    radius_spin.setToolTip("Target radius as a fraction of the task region's smaller dimension.")
    hold_spin.setToolTip("How long the cursor must stay inside the target to complete it.")
    reward_channel_spin.setToolTip("Reward output channel used when this target succeeds.")
    static_color_button.setToolTip("Choose the target color when the cursor is outside it.")
    static_opacity_spin.setToolTip("Target opacity when the cursor is outside it.")
    active_color_button.setToolTip("Choose the target color when the cursor is inside it.")
    active_opacity_spin.setToolTip("Target opacity when the cursor is inside it.")
    enable_drag_box.setToolTip("Disable this to select targets in the preview without accidentally moving them.")

    form_layout.addRow("Name", name_edit)
    form_layout.addRow("", enabled_box)
    form_layout.addRow("X", x_spin)
    form_layout.addRow("Y", y_spin)
    form_layout.addRow("Radius", radius_spin)
    form_layout.addRow("Hold (s)", hold_spin)
    form_layout.addRow("Reward Channel", reward_channel_spin)
    form_layout.addRow("Static Color", static_color_button)
    form_layout.addRow("Static Opacity", static_opacity_spin)
    form_layout.addRow("Active Color", active_color_button)
    form_layout.addRow("Active Opacity", active_opacity_spin)

    cursor_panel = QGroupBox("Cursor Reference")
    cursor_layout = QFormLayout(cursor_panel)
    cursor_layout.setContentsMargins(6, 10, 6, 6)
    cursor_layout.setHorizontalSpacing(6)
    cursor_layout.setVerticalSpacing(3)
    cursor_radius_spin = QDoubleSpinBox()
    cursor_radius_spin.setRange(0.005, 0.5)
    cursor_radius_spin.setDecimals(3)
    cursor_radius_spin.setSingleStep(0.005)
    cursor_radius_spin.setValue(draft_cursor_radius)
    cursor_radius_spin.setToolTip("Reference cursor radius shown in the layout preview center.")
    cursor_layout.addRow("Cursor Radius", cursor_radius_spin)
    cursor_layout.addRow("", enable_drag_box)

    generator_panel = QGroupBox("Target Generator")
    generator_layout = QFormLayout(generator_panel)
    generator_layout.setContentsMargins(6, 10, 6, 6)
    generator_layout.setHorizontalSpacing(6)
    generator_layout.setVerticalSpacing(3)
    generator_layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
    generator_mode_combo = QComboBox()
    generator_mode_combo.addItem("Annulus / Rings", "annulus")
    generator_mode_combo.addItem("Rectangular Grid", "grid")
    generator_mode_combo.addItem("Hexagonal Packing", "hex")
    generator_operation_combo = QComboBox()
    generator_operation_combo.addItem("Append", "append")
    generator_operation_combo.addItem("Replace", "replace")
    name_prefix_edit = QLineEdit("Target")
    generator_style_source_combo = QComboBox()
    generator_style_source_combo.addItem("Explicit style below", "explicit")
    generator_style_source_combo.addItem("Inherit from selected target", "inherit")
    generator_mode_combo.setToolTip("Choose how target coordinates are generated.")
    generator_operation_combo.setToolTip("Append adds to the current draft. Replace swaps the draft for generated targets when you apply the preview.")
    name_prefix_edit.setToolTip("Base name used when new generated targets are created.")
    generator_style_source_combo.setToolTip(
      "Explicit: every generated target uses the size, hold, reward, and colors set below. "
      "Inherit: copy those from the selected target (or the first target)."
    )

    annulus_ring_count_spin = QSpinBox()
    annulus_ring_count_spin.setRange(1, 20)
    annulus_ring_count_spin.setValue(2)
    annulus_ring_count_spin.setToolTip("Number of concentric rings to generate.")
    annulus_points_per_ring_spin = QSpinBox()
    annulus_points_per_ring_spin.setRange(1, 64)
    annulus_points_per_ring_spin.setValue(8)
    annulus_points_per_ring_spin.setToolTip("Base number of targets on the first ring. Outer rings scale up from this value.")
    annulus_inner_radius_spin = QDoubleSpinBox()
    annulus_inner_radius_spin.setRange(0.0, 0.70)
    annulus_inner_radius_spin.setDecimals(3)
    annulus_inner_radius_spin.setSingleStep(0.01)
    annulus_inner_radius_spin.setValue(0.22)
    annulus_inner_radius_spin.setToolTip("Distance from center to the innermost ring. Useful for keeping the center clean.")
    annulus_outer_radius_spin = QDoubleSpinBox()
    annulus_outer_radius_spin.setRange(0.0, 0.70)
    annulus_outer_radius_spin.setDecimals(3)
    annulus_outer_radius_spin.setSingleStep(0.01)
    annulus_outer_radius_spin.setValue(0.38)
    annulus_outer_radius_spin.setToolTip("Distance from center to the outermost ring.")
    annulus_angle_offset_spin = QDoubleSpinBox()
    annulus_angle_offset_spin.setRange(0.0, 360.0)
    annulus_angle_offset_spin.setDecimals(1)
    annulus_angle_offset_spin.setSingleStep(5.0)
    annulus_angle_offset_spin.setValue(0.0)
    annulus_angle_offset_spin.setToolTip("Rotate the ring pattern around the center.")

    grid_rows_spin = QSpinBox()
    grid_rows_spin.setRange(1, 50)
    grid_rows_spin.setValue(6)
    grid_rows_spin.setToolTip("Number of target rows for grid and hex generators.")
    grid_columns_spin = QSpinBox()
    grid_columns_spin.setRange(1, 50)
    grid_columns_spin.setValue(8)
    grid_columns_spin.setToolTip("Number of target columns for grid and hex generators.")
    grid_margin_spin = QDoubleSpinBox()
    grid_margin_spin.setRange(0.0, 0.45)
    grid_margin_spin.setDecimals(3)
    grid_margin_spin.setSingleStep(0.01)
    grid_margin_spin.setValue(0.08)
    grid_margin_spin.setToolTip("Leaves a border so targets do not sit right against the task-region edge.")
    center_exclusion_spin = QDoubleSpinBox()
    center_exclusion_spin.setRange(0.0, 0.70)
    center_exclusion_spin.setDecimals(3)
    center_exclusion_spin.setSingleStep(0.01)
    center_exclusion_spin.setValue(0.18)
    center_exclusion_spin.setToolTip("Circular exclusion zone around center to prevent accidental idle hits.")

    # Explicit style applied to every generated target (used when Style Source == explicit).
    gen_static_color = list(DEFAULT_TARGET_COLOR)
    gen_active_color = list(DEFAULT_TARGET_ACTIVE_COLOR)
    gen_style_header = QLabel("Generated Target Style")
    gen_enabled_box = QCheckBox("Enabled")
    gen_enabled_box.setChecked(True)
    gen_enabled_box.setToolTip("Whether generated targets start enabled.")
    gen_radius_spin = QDoubleSpinBox()
    gen_radius_spin.setRange(0.01, 0.5)
    gen_radius_spin.setDecimals(3)
    gen_radius_spin.setSingleStep(0.01)
    gen_radius_spin.setValue(DEFAULT_TARGET_RADIUS_RATIO)
    gen_radius_spin.setToolTip("Radius (as a fraction of the region) for every generated target.")
    gen_hold_spin = QDoubleSpinBox()
    gen_hold_spin.setRange(0.01, 10.0)
    gen_hold_spin.setDecimals(3)
    gen_hold_spin.setSingleStep(0.05)
    gen_hold_spin.setValue(DEFAULT_TARGET_HOLD_TIME)
    gen_hold_spin.setToolTip("Hold time for every generated target.")
    gen_reward_channel_spin = QSpinBox()
    gen_reward_channel_spin.setRange(0, 255)
    gen_reward_channel_spin.setSingleStep(1)
    gen_reward_channel_spin.setValue(int(task_config.get("reward_channel", 0)))
    gen_reward_channel_spin.setToolTip("Reward channel for every generated target.")
    gen_static_color_button = QPushButton("Choose Static Color")
    gen_static_color_button.setToolTip("Static color for every generated target.")
    gen_static_opacity_spin = QDoubleSpinBox()
    gen_static_opacity_spin.setRange(0.0, 1.0)
    gen_static_opacity_spin.setDecimals(2)
    gen_static_opacity_spin.setSingleStep(0.05)
    gen_static_opacity_spin.setValue(DEFAULT_TARGET_OPACITY)
    gen_static_opacity_spin.setToolTip("Static opacity for every generated target.")
    gen_active_color_button = QPushButton("Choose Active Color")
    gen_active_color_button.setToolTip("Active color for every generated target.")
    gen_active_opacity_spin = QDoubleSpinBox()
    gen_active_opacity_spin.setRange(0.0, 1.0)
    gen_active_opacity_spin.setDecimals(2)
    gen_active_opacity_spin.setSingleStep(0.05)
    gen_active_opacity_spin.setValue(DEFAULT_TARGET_ACTIVE_OPACITY)
    gen_active_opacity_spin.setToolTip("Active opacity for every generated target.")

    def update_gen_color_buttons() -> None:
      gen_static_color_button.setStyleSheet(
        f"background-color: rgb({int(gen_static_color[0])}, {int(gen_static_color[1])}, {int(gen_static_color[2])});"
      )
      gen_active_color_button.setStyleSheet(
        f"background-color: rgb({int(gen_active_color[0])}, {int(gen_active_color[1])}, {int(gen_active_color[2])});"
      )

    def choose_gen_static_color() -> None:
      selected = QColorDialog.getColor(QColor(*gen_static_color), dialog, "Select Generated Static Color")
      if not selected.isValid():
        return
      gen_static_color[:] = [selected.red(), selected.green(), selected.blue()]
      update_gen_color_buttons()

    def choose_gen_active_color() -> None:
      selected = QColorDialog.getColor(QColor(*gen_active_color), dialog, "Select Generated Active Color")
      if not selected.isValid():
        return
      gen_active_color[:] = [selected.red(), selected.green(), selected.blue()]
      update_gen_color_buttons()

    gen_static_color_button.clicked.connect(choose_gen_static_color)
    gen_active_color_button.clicked.connect(choose_gen_active_color)

    generator_hint_label = QLabel("")
    generator_hint_label.setWordWrap(True)
    generator_hint_label.setToolTip("Short description of the currently selected generator mode.")
    preview_generator_button = QPushButton("Preview")
    apply_preview_button = QPushButton("Apply Preview")
    clear_preview_button = QPushButton("Clear Preview")
    preview_generator_button.setToolTip("Build a temporary preview in the canvas without changing the draft target list.")
    apply_preview_button.setToolTip("Commit the currently previewed generated targets using the selected Append or Replace operation.")
    clear_preview_button.setToolTip("Discard the current generator preview.")

    generator_layout.addRow("Mode", generator_mode_combo)
    generator_layout.addRow("Operation", generator_operation_combo)
    generator_layout.addRow("Name Prefix", name_prefix_edit)
    generator_layout.addRow("Rows", grid_rows_spin)
    generator_layout.addRow("Columns", grid_columns_spin)
    generator_layout.addRow("Ring Count", annulus_ring_count_spin)
    generator_layout.addRow("Base Points / Ring", annulus_points_per_ring_spin)
    generator_layout.addRow("Edge Margin", grid_margin_spin)
    generator_layout.addRow("Center Exclusion", center_exclusion_spin)
    generator_layout.addRow("Inner Radius", annulus_inner_radius_spin)
    generator_layout.addRow("Outer Radius", annulus_outer_radius_spin)
    generator_layout.addRow("Angle Offset", annulus_angle_offset_spin)
    generator_layout.addRow("Style Source", generator_style_source_combo)
    generator_layout.addRow(gen_style_header)
    generator_layout.addRow("", gen_enabled_box)
    generator_layout.addRow("Radius", gen_radius_spin)
    generator_layout.addRow("Hold (s)", gen_hold_spin)
    generator_layout.addRow("Reward Channel", gen_reward_channel_spin)
    generator_layout.addRow("Static Color", gen_static_color_button)
    generator_layout.addRow("Static Opacity", gen_static_opacity_spin)
    generator_layout.addRow("Active Color", gen_active_color_button)
    generator_layout.addRow("Active Opacity", gen_active_opacity_spin)
    generator_layout.addRow("", generator_hint_label)
    preview_button_row = QWidget()
    preview_button_row_layout = QHBoxLayout(preview_button_row)
    preview_button_row_layout.setContentsMargins(0, 0, 0, 0)
    preview_button_row_layout.addWidget(preview_generator_button)
    preview_button_row_layout.addWidget(apply_preview_button)
    preview_button_row_layout.addWidget(clear_preview_button)
    generator_layout.addRow("", preview_button_row)
    settings_tabs.addTab(make_settings_tab(form_panel, cursor_panel), "Target")
    settings_tabs.addTab(make_settings_tab(generator_panel), "Generator")
    settings_tabs.addTab(make_settings_tab(schedule_panel), "Schedule")
    side_layout.addWidget(settings_tabs, 1)

    # Two rows: the side panel is only ~300px wide, so seven buttons on one
    # line render squished past legibility.
    add_button = QPushButton("Add")
    remove_button = QPushButton("Remove")
    clear_all_button = QPushButton("Clear All")
    enable_all_button = QPushButton("Enable All")
    disable_all_button = QPushButton("Disable All")
    save_button = QPushButton("Save")
    cancel_button = QPushButton("Cancel")
    add_button.setToolTip("Duplicate the selected target, or add a new default target if nothing is selected.")
    remove_button.setToolTip("Remove every selected target from the draft.")
    clear_all_button.setToolTip("Remove all draft targets from the editor.")
    enable_all_button.setToolTip("Mark every draft target enabled (targets are kept, nothing is removed).")
    disable_all_button.setToolTip("Mark every draft target disabled (targets are kept, nothing is removed).")
    save_button.setToolTip("Commit draft targets back to the task configuration.")
    cancel_button.setToolTip("Close the editor without saving draft changes.")
    edit_button_row = QWidget()
    edit_button_row_layout = QHBoxLayout(edit_button_row)
    edit_button_row_layout.setContentsMargins(0, 0, 0, 0)
    edit_button_row_layout.addWidget(add_button)
    edit_button_row_layout.addWidget(remove_button)
    edit_button_row_layout.addWidget(clear_all_button)
    edit_button_row_layout.addStretch(1)
    action_button_row = QWidget()
    action_button_row_layout = QHBoxLayout(action_button_row)
    action_button_row_layout.setContentsMargins(0, 0, 0, 0)
    action_button_row_layout.addWidget(enable_all_button)
    action_button_row_layout.addWidget(disable_all_button)
    action_button_row_layout.addStretch(1)
    action_button_row_layout.addWidget(save_button)
    action_button_row_layout.addWidget(cancel_button)
    side_layout.addWidget(edit_button_row)
    side_layout.addWidget(action_button_row)

    def refresh_target_list() -> None:
      target_list.blockSignals(True)
      target_list.clear()
      for i, target in enumerate(draft_targets):
        label = str(target.get("name", "")).strip() or f"Target {i + 1}"
        if not bool(target.get("enabled", True)):
          label += " (disabled)"
        target_list.addItem(label)
      if 0 <= selected_index < target_list.count():
        target_list.setCurrentRow(selected_index)
      target_list.blockSignals(False)

    def update_color_buttons() -> None:
      if 0 <= selected_index < len(draft_targets):
        static_rgb = draft_targets[selected_index].get("target_color", DEFAULT_TARGET_COLOR)
        active_rgb = draft_targets[selected_index].get("target_active_color", DEFAULT_TARGET_ACTIVE_COLOR)
      else:
        static_rgb = DEFAULT_TARGET_COLOR
        active_rgb = DEFAULT_TARGET_ACTIVE_COLOR
      static_color_button.setStyleSheet(
        f"background-color: rgb({int(static_rgb[0])}, {int(static_rgb[1])}, {int(static_rgb[2])});"
      )
      active_color_button.setStyleSheet(
        f"background-color: rgb({int(active_rgb[0])}, {int(active_rgb[1])}, {int(active_rgb[2])});"
      )

    def populate_controls() -> None:
      controls_enabled = 0 <= selected_index < len(draft_targets)
      for widget in (
        name_edit,
        enabled_box,
        x_spin,
        y_spin,
        radius_spin,
        hold_spin,
        reward_channel_spin,
        static_color_button,
        static_opacity_spin,
        active_color_button,
        active_opacity_spin,
        remove_button,
      ):
        widget.setEnabled(controls_enabled)
      if not controls_enabled:
        target_list.blockSignals(True)
        name_edit.blockSignals(True)
        enabled_box.blockSignals(True)
        x_spin.blockSignals(True)
        y_spin.blockSignals(True)
        radius_spin.blockSignals(True)
        hold_spin.blockSignals(True)
        reward_channel_spin.blockSignals(True)
        static_opacity_spin.blockSignals(True)
        active_opacity_spin.blockSignals(True)
        name_edit.setText("")
        enabled_box.setChecked(False)
        x_spin.setValue(0.0)
        y_spin.setValue(0.0)
        radius_spin.setValue(DEFAULT_TARGET_RADIUS_RATIO)
        hold_spin.setValue(DEFAULT_TARGET_HOLD_TIME)
        reward_channel_spin.setValue(int(task_config.get("reward_channel", 0)))
        static_opacity_spin.setValue(DEFAULT_TARGET_OPACITY)
        active_opacity_spin.setValue(DEFAULT_TARGET_ACTIVE_OPACITY)
        active_opacity_spin.blockSignals(False)
        static_opacity_spin.blockSignals(False)
        reward_channel_spin.blockSignals(False)
        hold_spin.blockSignals(False)
        radius_spin.blockSignals(False)
        y_spin.blockSignals(False)
        x_spin.blockSignals(False)
        enabled_box.blockSignals(False)
        name_edit.blockSignals(False)
        target_list.blockSignals(False)
        update_color_buttons()
        if preview is not None:
          preview.set_selected_index(-1)
        if schedule_preview is not None:
          schedule_preview.set_selected_index(-1)
        return

      target = draft_targets[selected_index]
      target_list.blockSignals(True)
      name_edit.blockSignals(True)
      enabled_box.blockSignals(True)
      x_spin.blockSignals(True)
      y_spin.blockSignals(True)
      radius_spin.blockSignals(True)
      hold_spin.blockSignals(True)
      reward_channel_spin.blockSignals(True)
      static_opacity_spin.blockSignals(True)
      active_opacity_spin.blockSignals(True)
      name_edit.setText(str(target.get("name", "")))
      enabled_box.setChecked(bool(target.get("enabled", True)))
      x_spin.setValue(float(target.get("x_norm", 0.75)))
      y_spin.setValue(float(target.get("y_norm", 0.50)))
      radius_spin.setValue(float(target.get("radius_ratio", DEFAULT_TARGET_RADIUS_RATIO)))
      hold_spin.setValue(float(target.get("hold_time", DEFAULT_TARGET_HOLD_TIME)))
      reward_channel_spin.setValue(int(target.get("reward_channel", task_config.get("reward_channel", 0))))
      static_opacity_spin.setValue(float(target.get("target_opacity", DEFAULT_TARGET_OPACITY)))
      active_opacity_spin.setValue(float(target.get("target_active_opacity", DEFAULT_TARGET_ACTIVE_OPACITY)))
      active_opacity_spin.blockSignals(False)
      static_opacity_spin.blockSignals(False)
      reward_channel_spin.blockSignals(False)
      hold_spin.blockSignals(False)
      radius_spin.blockSignals(False)
      y_spin.blockSignals(False)
      x_spin.blockSignals(False)
      enabled_box.blockSignals(False)
      name_edit.blockSignals(False)
      update_color_buttons()
      target_list.setCurrentRow(selected_index)
      target_list.blockSignals(False)
      if preview is not None:
        preview.set_selected_index(selected_index)
      if schedule_preview is not None:
        schedule_preview.set_selected_index(selected_index)

    def update_selected_index_from_list() -> None:
      nonlocal selected_index
      index = target_list.currentRow()
      if index < 0 or index >= len(draft_targets):
        selected_index = -1
      else:
        selected_index = index
      populate_controls()
      refresh_schedule_controls()

    def refresh_schedule_controls() -> None:
      mode = str(draft_schedule.get("mode", "random"))
      schedule_mode_combo.blockSignals(True)
      mode_index = schedule_mode_combo.findData(mode)
      schedule_mode_combo.setCurrentIndex(max(0, mode_index))
      schedule_mode_combo.blockSignals(False)

      schedule_center_combo.blockSignals(True)
      schedule_center_combo.clear()
      for i, target in enumerate(draft_targets):
        name = str(target.get("name", "")).strip()
        schedule_center_combo.addItem(name or f"Target {i + 1}", name)
      center_name = str(draft_schedule.get("center", ""))
      center_index = schedule_center_combo.findData(center_name)
      if center_index < 0 and center_name:
        schedule_center_combo.addItem(f"{center_name} (missing)", center_name)
        center_index = schedule_center_combo.count() - 1
      schedule_center_combo.setCurrentIndex(max(0, center_index))
      schedule_center_combo.blockSignals(False)

      schedule_peripheral_order_combo.blockSignals(True)
      order_index = schedule_peripheral_order_combo.findData(
        str(draft_schedule.get("peripheral_order", "sequential")))
      schedule_peripheral_order_combo.setCurrentIndex(max(0, order_index))
      schedule_peripheral_order_combo.blockSignals(False)

      schedule_interleave_spin.blockSignals(True)
      schedule_interleave_spin.setValue(float(draft_schedule.get("interleave_random_ratio", 0.0)))
      schedule_interleave_spin.blockSignals(False)

      # Ring picker. Auto (all enabled non-center) is stored as an empty
      # peripherals list — exactly the Rust runtime rule — so the checkbox
      # state derives from the list being empty, except while the operator is
      # deliberately building an explicit ring from scratch (Uncheck All).
      peripherals = [str(name) for name in draft_schedule.get("peripherals", [])]
      auto_ring = not peripherals and not ring_explicit_when_empty
      schedule_ring_auto_box.blockSignals(True)
      schedule_ring_auto_box.setChecked(auto_ring)
      schedule_ring_auto_box.blockSignals(False)
      schedule_ring_list.blockSignals(True)
      schedule_ring_list.clear()
      checkable = Qt.ItemFlag.ItemIsSelectable | Qt.ItemFlag.ItemIsUserCheckable
      listed_names = set()
      for i, target in enumerate(draft_targets):
        name = str(target.get("name", ""))
        enabled = bool(target.get("enabled", True))
        if not name.strip() or name.strip() == center_name.strip():
          continue
        if auto_ring and not enabled:
          continue
        display = name.strip() if enabled else f"{name.strip()} (disabled)"
        item = QListWidgetItem(display)
        item.setData(Qt.ItemDataRole.UserRole, name)
        if auto_ring:
          item.setFlags(checkable)  # greyed: shows what Auto resolves to
          item.setCheckState(Qt.CheckState.Checked)
        else:
          item.setFlags(checkable | Qt.ItemFlag.ItemIsEnabled)
          item.setCheckState(
            Qt.CheckState.Checked if name in peripherals else Qt.CheckState.Unchecked)
        schedule_ring_list.addItem(item)
        listed_names.add(name)
      for name in peripherals:
        if name not in listed_names:
          item = QListWidgetItem(f"{name} (missing)")
          item.setData(Qt.ItemDataRole.UserRole, name)
          item.setFlags(checkable | Qt.ItemFlag.ItemIsEnabled)
          item.setCheckState(Qt.CheckState.Checked)
          schedule_ring_list.addItem(item)
      schedule_ring_list.blockSignals(False)

      is_sequence = mode == "sequence"
      is_center_out = mode == "center_out"

      # Per-group ring checkboxes: include/exclude every enabled target of a
      # preset group at once. Same rebuild idiom as the Groups panel.
      while schedule_ring_group_row_layout.count():
        row_item = schedule_ring_group_row_layout.takeAt(0)
        row_widget = row_item.widget()
        if row_widget is not None:
          row_widget.setParent(None)
          row_widget.deleteLater()
      ring_set = set(compute_auto_ring()) if auto_ring else set(peripherals)
      ring_group_members: typing.Dict[str, typing.List[str]] = {}
      for target in draft_targets:
        name = str(target.get("name", ""))
        if not name.strip() or name.strip() == center_name.strip():
          continue
        if not bool(target.get("enabled", True)):
          continue
        ring_group_members.setdefault(target_group_key(target), []).append(name)
      has_ring_groups = any(key != GROUPLESS_LABEL for key in ring_group_members)
      schedule_ring_group_row.setVisible(is_center_out and has_ring_groups)
      if has_ring_groups:
        for key in sorted(ring_group_members):
          members = ring_group_members[key]
          in_ring = sum(1 for member in members if member in ring_set)
          group_box = QCheckBox(f"{key} ({in_ring}/{len(members)})")
          group_box.setToolTip(
            f"Include/exclude every enabled '{key}' target in the ring. Excluded targets "
            "stay enabled — they appear only via random inserts."
          )
          group_box.blockSignals(True)
          group_box.setChecked(in_ring == len(members))
          group_box.blockSignals(False)
          group_box.clicked.connect(
            lambda checked, group=key: set_ring_group(group, bool(checked)))
          schedule_ring_group_row_layout.addWidget(group_box)
      schedule_ring_group_row_layout.addStretch(1)
      schedule_ring_hint_label.setVisible(is_center_out and not auto_ring and not peripherals)

      has_selection = 0 <= selected_index < len(draft_targets)
      schedule_center_combo.setEnabled(is_center_out and schedule_center_combo.count() > 0)
      schedule_peripheral_order_combo.setEnabled(is_center_out)
      schedule_ring_auto_box.setEnabled(is_center_out)
      schedule_ring_list.setEnabled(is_center_out)
      schedule_ring_check_all_button.setEnabled(is_center_out)
      schedule_ring_uncheck_all_button.setEnabled(is_center_out)
      schedule_ring_group_row.setEnabled(is_center_out)
      schedule_move_up_button.setEnabled(is_sequence and has_selection and selected_index > 0)
      schedule_move_down_button.setEnabled(
        is_sequence and has_selection and selected_index < len(draft_targets) - 1)
      schedule_interleave_spin.setEnabled(is_sequence or is_center_out)
      if schedule_preview is not None:
        schedule_preview.update()

    def set_draft_group_enabled(group: str, value: bool) -> None:
      for i in range(len(draft_targets)):
        target = normalize_target(draft_targets[i])
        if target_group_key(target) == group:
          target["enabled"] = value
        draft_targets[i] = target
      refresh_editor()

    def refresh_group_panel() -> None:
      rebuild_group_toggle_rows(
        group_panel_box, group_panel_layout, draft_targets, set_draft_group_enabled)

    def refresh_editor() -> None:
      refresh_target_list()
      populate_controls()
      refresh_schedule_controls()
      refresh_group_panel()
      if preview is not None:
        preview.update()

    def update_preview_buttons() -> None:
      has_preview = bool(pending_generated_targets)
      apply_preview_button.setEnabled(has_preview)
      clear_preview_button.setEnabled(has_preview)

    def update_generator_controls() -> None:
      mode = str(generator_mode_combo.currentData())
      is_annulus = mode == "annulus"
      annulus_ring_count_spin.setEnabled(is_annulus)
      annulus_points_per_ring_spin.setEnabled(is_annulus)
      annulus_inner_radius_spin.setEnabled(is_annulus)
      annulus_outer_radius_spin.setEnabled(is_annulus)
      annulus_angle_offset_spin.setEnabled(is_annulus)
      grid_rows_spin.setEnabled(not is_annulus)
      grid_columns_spin.setEnabled(not is_annulus)
      center_exclusion_spin.setEnabled(not is_annulus)
      grid_margin_spin.setEnabled(not is_annulus)
      explicit_style = str(generator_style_source_combo.currentData()) == "explicit"
      for style_widget in (
        gen_enabled_box,
        gen_radius_spin,
        gen_hold_spin,
        gen_reward_channel_spin,
        gen_static_color_button,
        gen_static_opacity_spin,
        gen_active_color_button,
        gen_active_opacity_spin,
      ):
        style_widget.setEnabled(explicit_style)
      if is_annulus:
        generator_hint_label.setText(
          "Annulus places targets in concentric rings and naturally leaves the center open."
        )
      elif mode == "hex":
        generator_hint_label.setText(
          "Hexagonal packing gives denser, more uniform coverage than a rectangular grid."
        )
      else:
        generator_hint_label.setText(
          "Rectangular grid covers the task region evenly while respecting the center exclusion zone."
        )

    preview = LayoutPreview(
      draft_targets,
      lambda: pending_generated_targets,
      lambda: draft_cursor_radius,
      lambda index: target_list.setCurrentRow(index),
      refresh_editor,
      dialog,
    )
    preview.setToolTip("Solid targets are in the draft. Dashed blue targets are generator previews.")
    preview.set_drag_enabled(enable_drag_box.isChecked())
    schedule_preview = SchedulePreview(
      draft_targets,
      lambda: build_draft_schedule(),
      lambda: draft_cursor_radius,
      lambda index: target_list.setCurrentRow(index),
      refresh_editor,
      dialog,
    )
    schedule_preview.setToolTip(
      "Schedule-only view: badges show the center (C) and the ring/sequence visit order. "
      "Faint targets are not part of the structured schedule."
    )
    preview_tabs = QTabWidget(dialog)
    preview_tabs.addTab(preview, "Layout")
    preview_tabs.addTab(schedule_preview, "Schedule")
    dialog_layout.addWidget(preview_tabs, 1)
    dialog_layout.addWidget(side_panel)

    def apply_field_changes() -> None:
      if not (0 <= selected_index < len(draft_targets)):
        return
      old_name = str(draft_targets[selected_index].get("name", ""))
      draft_targets[selected_index] = normalize_target({
        **draft_targets[selected_index],
        "name": name_edit.text(),
        "enabled": enabled_box.isChecked(),
        "x_norm": x_spin.value(),
        "y_norm": y_spin.value(),
        "radius_ratio": radius_spin.value(),
        "hold_time": hold_spin.value(),
        "reward_channel": reward_channel_spin.value(),
        "target_opacity": static_opacity_spin.value(),
        "target_active_opacity": active_opacity_spin.value(),
      })
      new_name = str(draft_targets[selected_index].get("name", ""))
      # Keep schedule references (center/peripherals/order) pointing at a
      # renamed target — but only when no other target still bears the old
      # name (duplicate guard). Fires per keystroke, so references track live.
      if old_name and new_name != old_name and not any(
        str(target.get("name", "")) == old_name for target in draft_targets
      ):
        rename_in_schedule(draft_schedule, old_name, new_name)
      clear_generated_preview()
      refresh_editor()

    def choose_static_color() -> None:
      if not (0 <= selected_index < len(draft_targets)):
        return
      current_rgb = draft_targets[selected_index].get("target_color", DEFAULT_TARGET_COLOR)
      selected = QColorDialog.getColor(QColor(*current_rgb), dialog, "Select Static Target Color")
      if not selected.isValid():
        return
      draft_targets[selected_index]["target_color"] = [selected.red(), selected.green(), selected.blue()]
      clear_generated_preview()
      refresh_editor()

    def choose_active_color() -> None:
      if not (0 <= selected_index < len(draft_targets)):
        return
      current_rgb = draft_targets[selected_index].get("target_active_color", DEFAULT_TARGET_ACTIVE_COLOR)
      selected = QColorDialog.getColor(QColor(*current_rgb), dialog, "Select Active Target Color")
      if not selected.isValid():
        return
      draft_targets[selected_index]["target_active_color"] = [selected.red(), selected.green(), selected.blue()]
      clear_generated_preview()
      refresh_editor()

    name_edit.textEdited.connect(lambda _text: apply_field_changes())
    enabled_box.toggled.connect(lambda _checked: apply_field_changes())
    x_spin.editingFinished.connect(apply_field_changes)
    y_spin.editingFinished.connect(apply_field_changes)
    radius_spin.editingFinished.connect(apply_field_changes)
    hold_spin.editingFinished.connect(apply_field_changes)
    reward_channel_spin.editingFinished.connect(apply_field_changes)
    static_opacity_spin.editingFinished.connect(apply_field_changes)
    active_opacity_spin.editingFinished.connect(apply_field_changes)
    static_color_button.clicked.connect(choose_static_color)
    active_color_button.clicked.connect(choose_active_color)
    target_list.itemSelectionChanged.connect(update_selected_index_from_list)

    def on_cursor_radius_changed(value: float) -> None:
      nonlocal draft_cursor_radius
      draft_cursor_radius = max(0.005, min(0.5, float(value)))
      if preview is not None:
        preview.update()

    cursor_radius_spin.valueChanged.connect(on_cursor_radius_changed)

    def on_drag_enabled_changed(enabled: bool) -> None:
      task_config["target_layout_editor_drag_enabled"] = bool(enabled)
      preview.set_drag_enabled(enabled)

    enable_drag_box.toggled.connect(on_drag_enabled_changed)

    def on_schedule_mode_changed(_index: int) -> None:
      draft_schedule["mode"] = str(schedule_mode_combo.currentData())
      if draft_schedule["mode"] == "center_out" and not draft_schedule.get("center") and draft_targets:
        draft_schedule["center"] = str(draft_targets[0].get("name", ""))
      refresh_schedule_controls()

    def on_schedule_center_changed(_index: int) -> None:
      data = schedule_center_combo.currentData()
      if data is None:
        return
      draft_schedule["center"] = str(data)
      # A target cannot be both the center and a ring member.
      draft_schedule["peripherals"] = [
        name for name in draft_schedule.get("peripherals", []) if str(name) != str(data)
      ]
      refresh_schedule_controls()

    def on_schedule_peripheral_order_changed(_index: int) -> None:
      draft_schedule["peripheral_order"] = str(schedule_peripheral_order_combo.currentData())
      refresh_schedule_controls()

    def on_schedule_interleave_changed(value: float) -> None:
      draft_schedule["interleave_random_ratio"] = clamp(float(value), 0.0, 1.0)
      refresh_schedule_controls()

    def compute_auto_ring() -> typing.List[str]:
      center_name = str(draft_schedule.get("center", ""))
      return [
        str(target.get("name", "")) for target in draft_targets
        if bool(target.get("enabled", True)) and str(target.get("name", "")).strip()
        and str(target.get("name", "")).strip() != center_name.strip()
      ]

    def set_ring_explicit(names: typing.List[str]) -> None:
      nonlocal ring_explicit_when_empty
      draft_schedule["peripherals"] = names
      ring_explicit_when_empty = not names
      refresh_schedule_controls()

    def on_schedule_ring_auto_toggled(checked: bool) -> None:
      nonlocal ring_explicit_when_empty
      if checked:
        ring_explicit_when_empty = False
        draft_schedule["peripherals"] = []
        refresh_schedule_controls()
      else:
        # Seed the explicit list from what Auto resolves to, so the operator
        # prunes from a full ring rather than building from nothing (Uncheck
        # All is the build-from-nothing path).
        set_ring_explicit(compute_auto_ring())

    def on_schedule_ring_item_changed(_item: typing.Any) -> None:
      if schedule_ring_auto_box.isChecked():
        return
      names = []
      for row in range(schedule_ring_list.count()):
        item = schedule_ring_list.item(row)
        if item is not None and item.checkState() == Qt.CheckState.Checked:
          names.append(str(item.data(Qt.ItemDataRole.UserRole)))
      set_ring_explicit(names)

    def eligible_ring_group_members(group: str) -> typing.List[str]:
      center_name = str(draft_schedule.get("center", "")).strip()
      return [
        str(target.get("name", "")) for target in draft_targets
        if bool(target.get("enabled", True)) and str(target.get("name", "")).strip()
        and str(target.get("name", "")).strip() != center_name
        and target_group_key(target) == group
      ]

    def set_ring_group(group: str, include: bool) -> None:
      peripherals = [str(name) for name in draft_schedule.get("peripherals", [])]
      auto_ring = not peripherals and not ring_explicit_when_empty
      # From Auto, unchecking a group means "everything except that group",
      # so materialize the auto ring first and edit it explicitly.
      base = compute_auto_ring() if auto_ring else peripherals
      members = eligible_ring_group_members(group)
      if include:
        new_ring = base + [member for member in members if member not in base]
      else:
        member_set = set(members)
        new_ring = [name for name in base if name not in member_set]
      set_ring_explicit(new_ring)

    def move_selected_target(delta: int) -> None:
      nonlocal selected_index
      source = selected_index
      destination = source + delta
      if not (0 <= source < len(draft_targets) and 0 <= destination < len(draft_targets)):
        return
      draft_targets[source], draft_targets[destination] = draft_targets[destination], draft_targets[source]
      selected_index = destination
      clear_generated_preview()
      refresh_editor()

    schedule_mode_combo.currentIndexChanged.connect(on_schedule_mode_changed)
    schedule_center_combo.currentIndexChanged.connect(on_schedule_center_changed)
    schedule_peripheral_order_combo.currentIndexChanged.connect(on_schedule_peripheral_order_changed)
    schedule_interleave_spin.valueChanged.connect(on_schedule_interleave_changed)
    schedule_ring_auto_box.toggled.connect(on_schedule_ring_auto_toggled)
    schedule_ring_list.itemChanged.connect(on_schedule_ring_item_changed)
    schedule_ring_check_all_button.clicked.connect(lambda: set_ring_explicit(compute_auto_ring()))
    schedule_ring_uncheck_all_button.clicked.connect(lambda: set_ring_explicit([]))
    schedule_move_up_button.clicked.connect(lambda: move_selected_target(-1))
    schedule_move_down_button.clicked.connect(lambda: move_selected_target(1))

    def build_draft_schedule(
      source_targets: typing.Optional[typing.List[typing.Dict[str, typing.Any]]] = None,
    ) -> typing.Dict[str, typing.Any]:
      source = draft_targets if source_targets is None else source_targets
      schedule = normalize_schedule(draft_schedule)
      # Defense in depth: drop references to deleted targets and never list
      # the center as its own peripheral. Pruned-to-empty degrades to Auto.
      prune_schedule_names(schedule, {str(target.get("name", "")) for target in source})
      schedule["peripherals"] = [
        name for name in schedule["peripherals"] if name != schedule["center"]
      ]
      # Sequence order is the enabled targets in list order (reorder with
      # Move Up / Move Down). Written on every save so the config documents it.
      schedule["order"] = [
        str(target.get("name", "")) for target in source if bool(target.get("enabled", True))
      ]
      return schedule

    def add_layout_target() -> None:
      nonlocal selected_index
      if 0 <= selected_index < len(draft_targets):
        new_target = normalize_target(draft_targets[selected_index])
        new_target["name"] = f"{new_target.get('name', '').strip() or 'Target'} Copy"
      else:
        new_target = make_default_target(len(draft_targets) + 1)
      draft_targets.append(new_target)
      selected_index = len(draft_targets) - 1
      clear_generated_preview()
      refresh_editor()

    def remove_layout_target() -> None:
      nonlocal selected_index
      selected_rows = sorted({index.row() for index in target_list.selectedIndexes()}, reverse=True)
      if not selected_rows and 0 <= selected_index < len(draft_targets):
        selected_rows = [selected_index]
      if not selected_rows:
        return
      for row in selected_rows:
        if 0 <= row < len(draft_targets):
          del draft_targets[row]
      selected_index = min(selected_index, len(draft_targets) - 1) if draft_targets else -1
      prune_schedule_names(draft_schedule, {str(target.get("name", "")) for target in draft_targets})
      clear_generated_preview()
      refresh_editor()

    def clear_all_layout_targets() -> None:
      nonlocal selected_index
      draft_targets[:] = []
      selected_index = -1
      prune_schedule_names(draft_schedule, set())
      clear_generated_preview()
      refresh_editor()

    def set_all_layout_enabled(value: bool) -> None:
      for i in range(len(draft_targets)):
        draft_targets[i] = normalize_target(draft_targets[i])
        draft_targets[i]["enabled"] = value
      refresh_editor()

    def make_generator_template() -> typing.Dict[str, typing.Any]:
      if str(generator_style_source_combo.currentData()) == "explicit":
        return normalize_target({
          "enabled": gen_enabled_box.isChecked(),
          "radius_ratio": gen_radius_spin.value(),
          "hold_time": gen_hold_spin.value(),
          "reward_channel": gen_reward_channel_spin.value(),
          "target_color": list(gen_static_color),
          "target_opacity": gen_static_opacity_spin.value(),
          "target_active_color": list(gen_active_color),
          "target_active_opacity": gen_active_opacity_spin.value(),
        })
      if 0 <= selected_index < len(draft_targets):
        return normalize_target(draft_targets[selected_index])
      if draft_targets:
        return normalize_target(draft_targets[0])
      return make_default_target(1)

    def build_generated_targets() -> typing.List[typing.Dict[str, typing.Any]]:
      mode = str(generator_mode_combo.currentData())
      if mode == "annulus":
        points = generate_annulus_points(
          annulus_ring_count_spin.value(),
          annulus_points_per_ring_spin.value(),
          annulus_inner_radius_spin.value(),
          annulus_outer_radius_spin.value(),
          annulus_angle_offset_spin.value(),
        )
      elif mode == "hex":
        points = generate_hexagonal_points(
          grid_rows_spin.value(),
          grid_columns_spin.value(),
          grid_margin_spin.value(),
          center_exclusion_spin.value(),
        )
      else:
        points = generate_rectangular_grid_points(
          grid_rows_spin.value(),
          grid_columns_spin.value(),
          grid_margin_spin.value(),
          center_exclusion_spin.value(),
        )

      if not points:
        return []

      template_target = make_generator_template()
      prefix = name_prefix_edit.text().strip() or "Target"
      start_index = 1 if str(generator_operation_combo.currentData()) == "replace" else len(draft_targets) + 1
      return [
        make_target_from_template(template_target, start_index + i, x, y, prefix)
        for i, (x, y) in enumerate(points)
      ]

    def preview_generated_targets() -> None:
      nonlocal pending_generator_operation
      generated_targets = build_generated_targets()
      if not generated_targets:
        QMessageBox.warning(
          dialog,
          "No Targets Generated",
          "The current generator settings produced no valid targets. Adjust the margin or center exclusion values.",
        )
        return
      pending_generated_targets[:] = generated_targets
      pending_generator_operation = str(generator_operation_combo.currentData())
      update_preview_buttons()
      # Generated previews are drawn on the Layout tab only — bring it to the
      # front so Preview never looks like a no-op from the Schedule tab.
      preview_tabs.setCurrentIndex(0)
      if preview is not None:
        preview.update()

    def clear_generated_preview() -> None:
      pending_generated_targets.clear()
      update_preview_buttons()
      if preview is not None:
        preview.update()

    def apply_generated_preview() -> None:
      nonlocal selected_index
      if not pending_generated_targets:
        return
      if pending_generator_operation == "replace":
        draft_targets[:] = [normalize_target(target) for target in pending_generated_targets]
      else:
        draft_targets.extend(normalize_target(target) for target in pending_generated_targets)
      selected_index = len(draft_targets) - 1 if draft_targets else -1
      clear_generated_preview()
      refresh_editor()

    def save_layout() -> None:
      normalized_targets = [normalize_target(target) for target in draft_targets]
      while targets:
        del targets[len(targets) - 1]
      for target in normalized_targets:
        targets.append(target)
      task_config["cursor_diameter_ratio"] = max(0.01, min(1.0, draft_cursor_radius * 2.0))
      task_config["target_schedule"] = build_draft_schedule(normalized_targets)
      # Restart the structured schedule from the top: these are the cross-trial
      # cursor keys the Rust executor persists via config_updates.
      task_config["_schedule_seq_pos"] = 0
      task_config["_schedule_expect_center"] = True
      task_config["_schedule_peripheral_pos"] = 0
      sync_table_from_config()
      if 0 <= selected_index < target_table.rowCount():
        target_table.selectRow(selected_index)
      dialog.accept()

    def load_presets_dict() -> typing.Dict[str, typing.Any]:
      raw = task_config.get("target_layout_presets", {})
      if isinstance(raw, ObservableCollection):
        raw = raw.unwrap()
      return dict(raw) if isinstance(raw, dict) else {}

    def refresh_preset_combo(select_name: typing.Optional[str] = None) -> None:
      presets = load_presets_dict()
      preset_combo.blockSignals(True)
      preset_combo.clear()
      for name in sorted(presets.keys()):
        preset_combo.addItem(str(name))
      if select_name is not None:
        index = preset_combo.findText(select_name)
        if index >= 0:
          preset_combo.setCurrentIndex(index)
      preset_combo.blockSignals(False)
      has_presets = preset_combo.count() > 0
      preset_load_button.setEnabled(has_presets)
      preset_append_button.setEnabled(has_presets)
      preset_delete_button.setEnabled(has_presets)

    def save_preset() -> None:
      name, ok = QInputDialog.getText(dialog, "Save Preset", "Preset name:")
      if not ok:
        return
      name = name.strip()
      if not name:
        return
      presets = load_presets_dict()
      if name in presets:
        confirm = QMessageBox.question(
          dialog, "Overwrite Preset", f"Preset '{name}' already exists. Overwrite it?"
        )
        if confirm != QMessageBox.StandardButton.Yes:
          return
      presets[name] = {
        "targets": [normalize_target(target) for target in draft_targets],
        "schedule": build_draft_schedule(),
      }
      task_config["target_layout_presets"] = presets
      refresh_preset_combo(name)

    def read_preset_entry(
      name: str,
    ) -> typing.Tuple[typing.Optional[typing.List[typing.Any]], typing.Any]:
      stored = load_presets_dict().get(name)
      if isinstance(stored, ObservableCollection):
        stored = stored.unwrap()
      # Backward compatibility: presets saved before schedules were a bare
      # targets list; newer ones are {"targets": [...], "schedule": {...}}.
      if isinstance(stored, list):
        preset_targets: typing.Any = stored
        preset_schedule: typing.Any = {}
      elif isinstance(stored, dict):
        preset_targets = stored.get("targets", [])
        preset_schedule = stored.get("schedule", {})
      else:
        return None, {}
      if not isinstance(preset_targets, list):
        return None, {}
      return preset_targets, preset_schedule

    def tag_preset_targets(
      preset_targets: typing.List[typing.Any],
      preset_name: str,
    ) -> typing.List[typing.Dict[str, typing.Any]]:
      # Stamp untagged targets with the preset they came from; targets that
      # already carry a group (a combined layout saved as its own preset)
      # keep their original subgroup.
      tagged = []
      for raw in preset_targets:
        target = normalize_target(raw)
        if not target.get("group"):
          target["group"] = preset_name
        tagged.append(target)
      return tagged

    def load_preset() -> None:
      nonlocal selected_index, ring_explicit_when_empty
      name = preset_combo.currentText()
      if not name:
        return
      preset_targets, preset_schedule = read_preset_entry(name)
      if preset_targets is None:
        return
      if draft_targets:
        confirm = QMessageBox.question(
          dialog,
          "Load Preset",
          f"Replace the current {len(draft_targets)} draft target(s) with preset '{name}'?",
        )
        if confirm != QMessageBox.StandardButton.Yes:
          return
      draft_targets[:] = tag_preset_targets(preset_targets, name)
      draft_schedule.clear()
      draft_schedule.update(normalize_schedule(preset_schedule))
      ring_explicit_when_empty = False
      selected_index = 0 if draft_targets else -1
      clear_generated_preview()
      refresh_editor()

    def append_preset() -> None:
      nonlocal selected_index, ring_explicit_when_empty
      name = preset_combo.currentText()
      if not name:
        return
      preset_targets, preset_schedule = read_preset_entry(name)
      if preset_targets is None:
        return
      incoming = tag_preset_targets(preset_targets, name)
      if not incoming:
        return

      confirm = QDialog(dialog)
      confirm.setWindowTitle("Append Preset")
      confirm_layout = QVBoxLayout(confirm)
      confirm_layout.addWidget(QLabel(
        f"Append {len(incoming)} target(s) from preset '{name}' "
        f"to the current {len(draft_targets)}?"
      ))
      adopt_schedule_box = QCheckBox("Also adopt this preset's schedule")
      adopt_schedule_box.setToolTip(
        "Replace the draft schedule (mode/center/ring/random insert %) with the "
        "preset's saved schedule. Unchecked: only targets are appended."
      )
      confirm_layout.addWidget(adopt_schedule_box)
      confirm_button_row = QWidget()
      confirm_button_row_layout = QHBoxLayout(confirm_button_row)
      confirm_button_row_layout.setContentsMargins(0, 0, 0, 0)
      confirm_ok_button = QPushButton("Append")
      confirm_cancel_button = QPushButton("Cancel")
      confirm_ok_button.setDefault(True)
      confirm_button_row_layout.addStretch(1)
      confirm_button_row_layout.addWidget(confirm_ok_button)
      confirm_button_row_layout.addWidget(confirm_cancel_button)
      confirm_layout.addWidget(confirm_button_row)
      confirm_ok_button.clicked.connect(confirm.accept)
      confirm_cancel_button.clicked.connect(confirm.reject)
      if not confirm.exec():
        return

      # Schedules resolve targets by first-match-on-name (and the auto ring is
      # name-based), so a name collision would silently misbehave: rename the
      # appended copies.
      taken = {
        str(target.get("name", "")).strip()
        for target in draft_targets
        if str(target.get("name", "")).strip()
      }
      for target in incoming:
        target_name = str(target.get("name", "")).strip()
        if not target_name or target_name not in taken:
          taken.add(target_name)
          continue
        candidate = f"{target_name} ({name})"
        suffix = 2
        while candidate in taken:
          candidate = f"{target_name} ({name} {suffix})"
          suffix += 1
        target["name"] = candidate
        taken.add(candidate)

      draft_targets.extend(incoming)
      if adopt_schedule_box.isChecked():
        draft_schedule.clear()
        draft_schedule.update(normalize_schedule(preset_schedule))
        ring_explicit_when_empty = False
      selected_index = len(draft_targets) - 1
      clear_generated_preview()
      refresh_editor()

    def delete_preset() -> None:
      name = preset_combo.currentText()
      if not name:
        return
      presets = load_presets_dict()
      if name not in presets:
        return
      confirm = QMessageBox.question(dialog, "Delete Preset", f"Delete preset '{name}'?")
      if confirm != QMessageBox.StandardButton.Yes:
        return
      del presets[name]
      task_config["target_layout_presets"] = presets
      refresh_preset_combo()

    preset_save_button.clicked.connect(save_preset)
    preset_load_button.clicked.connect(load_preset)
    preset_append_button.clicked.connect(append_preset)
    preset_delete_button.clicked.connect(delete_preset)

    add_button.clicked.connect(add_layout_target)
    remove_button.clicked.connect(remove_layout_target)
    clear_all_button.clicked.connect(clear_all_layout_targets)
    enable_all_button.clicked.connect(lambda: set_all_layout_enabled(True))
    disable_all_button.clicked.connect(lambda: set_all_layout_enabled(False))
    generator_mode_combo.currentIndexChanged.connect(lambda _index: update_generator_controls())
    generator_style_source_combo.currentIndexChanged.connect(lambda _index: update_generator_controls())
    preview_generator_button.clicked.connect(preview_generated_targets)
    apply_preview_button.clicked.connect(apply_generated_preview)
    clear_preview_button.clicked.connect(clear_generated_preview)
    save_button.clicked.connect(save_layout)
    cancel_button.clicked.connect(dialog.reject)

    no_wheel_filter = NoWheelChangeFilter(dialog)
    dialog._no_wheel_filter = no_wheel_filter # type: ignore[attr-defined]
    for spinbox in dialog.findChildren(QSpinBox):
      spinbox.installEventFilter(no_wheel_filter)
    for spinbox in dialog.findChildren(QDoubleSpinBox):
      spinbox.installEventFilter(no_wheel_filter)
    for combo in dialog.findChildren(QComboBox):
      combo.installEventFilter(no_wheel_filter)
    for slider in dialog.findChildren(QSlider):
      slider.installEventFilter(no_wheel_filter)

    refresh_editor()
    refresh_preset_combo()
    update_gen_color_buttons()
    update_generator_controls()
    update_preview_buttons()
    dialog.show()
    dialog.raise_()
    dialog.activateWindow()

  def write_table_row(row: int, target: typing.Dict[str, typing.Any]) -> None:
    enabled_item = QTableWidgetItem("")
    enabled_item.setFlags(
      Qt.ItemFlag.ItemIsSelectable
      | Qt.ItemFlag.ItemIsEnabled
      | Qt.ItemFlag.ItemIsUserCheckable
    )
    enabled_item.setCheckState(Qt.CheckState.Checked if target["enabled"] else Qt.CheckState.Unchecked)
    target_table.setItem(row, 0, enabled_item)
    target_table.setItem(row, 1, QTableWidgetItem(str(target.get("name", ""))))
    target_table.setItem(row, 2, QTableWidgetItem(f"{target['x_norm']:.3f}"))
    target_table.setItem(row, 3, QTableWidgetItem(f"{target['y_norm']:.3f}"))
    target_table.setItem(row, 4, QTableWidgetItem(f"{target['radius_ratio']:.3f}"))
    target_table.setItem(row, 5, QTableWidgetItem(f"{target['hold_time']:.3f}"))
    target_table.setItem(row, 6, QTableWidgetItem(str(int(target.get("reward_channel", task_config.get("reward_channel", 0))))))
    target_table.setItem(row, 8, QTableWidgetItem(f"{float(target.get('target_opacity', DEFAULT_TARGET_OPACITY)):.2f}"))
    target_table.setItem(row, 10, QTableWidgetItem(f"{float(target.get('target_active_opacity', DEFAULT_TARGET_ACTIVE_OPACITY)):.2f}"))

    def make_color_button(
      color_key: str,
      default_color: typing.Sequence[int],
      title: str,
    ) -> QPushButton:
      button = QPushButton()
      rgb = target[color_key]
      button.setStyleSheet(f"background-color: rgb({rgb[0]}, {rgb[1]}, {rgb[2]});")
      button.setText("")

      def on_pick_color() -> None:
        if row < 0 or row >= len(targets):
          return
        normalized = normalize_target(targets[row])
        current_rgb = normalized.get(color_key, list(default_color))
        selected = QColorDialog.getColor(QColor(*current_rgb), result, title)
        if selected.isValid():
          new_rgb = [selected.red(), selected.green(), selected.blue()]
          targets[row] = normalized
          targets[row][color_key] = new_rgb
          button.setStyleSheet(f"background-color: rgb({new_rgb[0]}, {new_rgb[1]}, {new_rgb[2]});")

      button.clicked.connect(on_pick_color)
      return button

    target_table.setCellWidget(row, 7, make_color_button("target_color", DEFAULT_TARGET_COLOR, "Select Static Target Color"))
    target_table.setCellWidget(row, 9, make_color_button("target_active_color", DEFAULT_TARGET_ACTIVE_COLOR, "Select Active Target Color"))

  def sync_table_from_config() -> None:
    target_table.blockSignals(True)
    target_table.setRowCount(0)
    for i, raw_target in enumerate(list(targets)):
      target = normalize_target(raw_target)
      targets[i] = target
      target_table.insertRow(i)
      write_table_row(i, target)
    target_table.blockSignals(False)
    refresh_group_controls()

  def sync_row_to_config(row: int) -> None:
    if row < 0 or row >= len(targets):
      return
    enabled_item = target_table.item(row, 0)
    name_item = target_table.item(row, 1)
    x_item = target_table.item(row, 2)
    y_item = target_table.item(row, 3)
    radius_item = target_table.item(row, 4)
    hold_item = target_table.item(row, 5)
    reward_channel_item = target_table.item(row, 6)
    static_opacity_item = target_table.item(row, 8)
    active_opacity_item = target_table.item(row, 10)
    if not all((enabled_item, name_item, x_item, y_item, radius_item, hold_item, reward_channel_item, static_opacity_item, active_opacity_item)):
      return
    try:
      x_val = clamp(float(x_item.text()), 0.0, 1.0)
      y_val = clamp(float(y_item.text()), 0.0, 1.0)
      radius_val = clamp(float(radius_item.text()), 0.01, 0.5)
      hold_val = clamp(float(hold_item.text()), 0.01, 10.0)
      reward_channel_val = max(0, int(float(reward_channel_item.text())))
      static_opacity_val = clamp(float(static_opacity_item.text()), 0.0, 1.0)
      active_opacity_val = clamp(float(active_opacity_item.text()), 0.0, 1.0)
    except ValueError:
      target_table.blockSignals(True)
      write_table_row(row, normalize_target(targets[row]))
      target_table.blockSignals(False)
      return
    normalized = normalize_target(targets[row])
    targets[row] = {
      "name": str(name_item.text()),
      "enabled": enabled_item.checkState() == Qt.CheckState.Checked,
      "group": normalized.get("group", ""),
      "x_norm": x_val,
      "y_norm": y_val,
      "radius_ratio": radius_val,
      "hold_time": hold_val,
      "reward_channel": reward_channel_val,
      "target_color": normalized.get("target_color", DEFAULT_TARGET_COLOR.copy()),
      "target_opacity": static_opacity_val,
      "target_active_color": normalized.get("target_active_color", DEFAULT_TARGET_ACTIVE_COLOR.copy()),
      "target_active_opacity": active_opacity_val,
    }
    target_table.blockSignals(True)
    write_table_row(row, targets[row])
    target_table.blockSignals(False)

  def on_item_changed(item: QTableWidgetItem) -> None:
    if item.column() in (7, 9):
      return
    sync_row_to_config(item.row())

  target_table.itemChanged.connect(on_item_changed)

  controls = QWidget()
  controls_layout = QHBoxLayout(controls)
  controls_layout.setContentsMargins(0, 0, 0, 0)
  add_target_button = QPushButton("Add Target")
  remove_target_button = QPushButton("Remove Selected")
  clear_targets_button = QPushButton("Clear All")
  enable_all_targets_button = QPushButton("Enable All")
  disable_all_targets_button = QPushButton("Disable All")
  edit_layout_button = QPushButton("Edit Layout...")
  bulk_field_combo = QComboBox()
  bulk_field_combo.addItems([
    "Enabled",
    "Name",
    "X",
    "Y",
    "Radius",
    "Hold (s)",
    "Reward Channel",
    "Static Color",
    "Static Opacity",
    "Active Color",
    "Active Opacity",
  ])
  bulk_field_combo.setCurrentText("Radius")
  apply_to_all_button = QPushButton("Apply Field to All")
  add_target_button.setToolTip("Add a new target. If a row is selected, the new target copies that row.")
  remove_target_button.setToolTip("Remove all selected rows from the target table.")
  clear_targets_button.setToolTip("Remove every target row from the table.")
  enable_all_targets_button.setToolTip("Check Enabled on every target row (nothing is removed).")
  disable_all_targets_button.setToolTip("Uncheck Enabled on every target row (nothing is removed).")
  edit_layout_button.setToolTip("Open the visual layout editor for dragging, previewing, and bulk generation.")
  bulk_field_combo.setToolTip("Choose which field from the selected row should be copied to all targets.")
  apply_to_all_button.setToolTip("Copy the chosen field from the selected row to every target.")
  controls_layout.addWidget(add_target_button)
  controls_layout.addWidget(remove_target_button)
  controls_layout.addWidget(clear_targets_button)
  controls_layout.addWidget(enable_all_targets_button)
  controls_layout.addWidget(disable_all_targets_button)
  controls_layout.addWidget(edit_layout_button)
  controls_layout.addWidget(QLabel("Field:"))
  controls_layout.addWidget(bulk_field_combo)
  controls_layout.addWidget(apply_to_all_button)
  controls_layout.addStretch(1)

  # Live group toggles (mid-session control surface): flipping a preset group
  # here edits the running task config, so the change reaches the executor on
  # the next trial without touching the layout editor.
  target_groups_box = QGroupBox("Target Groups")
  target_groups_box.setToolTip(
    "One checkbox per preset group. Click to enable/disable every target that "
    "was loaded or appended from that preset. Takes effect on the next trial."
  )
  target_groups_layout = QHBoxLayout(target_groups_box)
  target_groups_layout.setContentsMargins(6, 4, 6, 4)
  target_groups_box.setVisible(False)

  def add_target() -> None:
    selected_row = -1
    selected_rows = sorted({idx.row() for idx in target_table.selectedIndexes()})
    if selected_rows:
      selected_row = selected_rows[0]

    if 0 <= selected_row < len(targets):
      new_target = normalize_target(targets[selected_row])
    else:
      new_target = {
        **make_default_target(len(targets) + 1)
      }

    targets.append(new_target)
    sync_table_from_config()
    new_row = target_table.rowCount() - 1
    if new_row >= 0:
      target_table.selectRow(new_row)

  def remove_target() -> None:
    selected_rows = sorted({idx.row() for idx in target_table.selectedIndexes()}, reverse=True)
    if not selected_rows and len(targets) > 0:
      selected_rows = [len(targets) - 1]
    for row in selected_rows:
      if 0 <= row < len(targets):
        del targets[row]
    sync_table_from_config()

  def clear_targets() -> None:
    while targets:
      del targets[len(targets) - 1]
    sync_table_from_config()

  def set_all_enabled(value: bool) -> None:
    for i in range(len(targets)):
      target = normalize_target(targets[i])
      target["enabled"] = value
      targets[i] = target
    sync_table_from_config()

  def set_group_enabled(group: str, value: bool) -> None:
    for i in range(len(targets)):
      target = normalize_target(targets[i])
      if target_group_key(target) == group:
        target["enabled"] = value
        targets[i] = target
    sync_table_from_config()

  def refresh_group_controls() -> None:
    rebuild_group_toggle_rows(
      target_groups_box, target_groups_layout, list(targets), set_group_enabled)

  def apply_selected_field_to_all() -> None:
    if not targets:
      return
    selected_rows = sorted({idx.row() for idx in target_table.selectedIndexes()})
    if not selected_rows:
      return
    src_row = selected_rows[0]
    if src_row < 0 or src_row >= len(targets):
      return

    field = bulk_field_combo.currentText()
    source = normalize_target(targets[src_row])

    if field == "Enabled":
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["enabled"] = bool(source["enabled"])
    elif field == "Name":
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["name"] = str(source["name"])
    elif field == "X":
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["x_norm"] = float(source["x_norm"])
    elif field == "Y":
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["y_norm"] = float(source["y_norm"])
    elif field == "Radius":
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["radius_ratio"] = float(source["radius_ratio"])
    elif field == "Hold (s)":
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["hold_time"] = float(source["hold_time"])
    elif field == "Reward Channel":
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["reward_channel"] = int(source["reward_channel"])
    elif field == "Static Color":
      rgb = list(source["target_color"])
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["target_color"] = rgb.copy()
    elif field == "Static Opacity":
      opacity = float(source["target_opacity"])
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["target_opacity"] = opacity
    elif field == "Active Color":
      rgb = list(source["target_active_color"])
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["target_active_color"] = rgb.copy()
    elif field == "Active Opacity":
      opacity = float(source["target_active_opacity"])
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["target_active_opacity"] = opacity

    sync_table_from_config()
    if target_table.rowCount() > 0:
      target_table.selectRow(src_row)

  add_target_button.clicked.connect(add_target)
  remove_target_button.clicked.connect(remove_target)
  clear_targets_button.clicked.connect(clear_targets)
  enable_all_targets_button.clicked.connect(lambda: set_all_enabled(True))
  disable_all_targets_button.clicked.connect(lambda: set_all_enabled(False))
  edit_layout_button.clicked.connect(open_layout_editor)
  apply_to_all_button.clicked.connect(apply_selected_field_to_all)

  layout.addWidget(QLabel("Targets (rows = targets):"))
  layout.addWidget(target_table)
  layout.addWidget(controls)
  layout.addWidget(target_groups_box)

  animation_group = QGroupBox("Animation Settings")
  animation_layout = QGridLayout(animation_group)

  def add_anim_checkbox(row: int, label: str, key: str) -> None:
    box = QCheckBox(label)
    box.setChecked(bool(task_config.get(key, False)))
    box.toggled.connect(lambda v, k=key: task_config.update({k: bool(v)}))
    animation_layout.addWidget(box, row, 0, 1, 2)

  add_anim_checkbox(0, "Enable Animations (Master)", "animations_enabled")
  add_anim_checkbox(1, "Enable Task Animations", "task_animation_enabled")
  add_anim_checkbox(2, "Show Streak HUD", "show_streak_hud")

  animation_layout.addWidget(QLabel("Streak Bonus Threshold"), 3, 0)
  streak_threshold_spin = QSpinBox()
  streak_threshold_spin.setRange(0, 1000)
  streak_threshold_spin.setValue(int(max(0, int(task_config.get("streak_bonus_threshold", 0)))))
  streak_threshold_spin.valueChanged.connect(lambda v: task_config.update({"streak_bonus_threshold": int(v)}))
  animation_layout.addWidget(streak_threshold_spin, 3, 1)

  animation_layout.addWidget(QLabel("Bonus Reward Count"), 4, 0)
  bonus_count_spin = QSpinBox()
  bonus_count_spin.setRange(1, 20)
  bonus_count_spin.setValue(int(max(1, int(task_config.get("streak_bonus_reward_count", 1)))))
  bonus_count_spin.valueChanged.connect(lambda v: task_config.update({"streak_bonus_reward_count": int(v)}))
  animation_layout.addWidget(bonus_count_spin, 4, 1)

  add_anim_checkbox(5, "Reset Streak After Bonus", "streak_reset_on_bonus")
  add_anim_checkbox(6, "Enable Target Animations", "target_animation_enabled")

  add_anim_checkbox(7, "Show Hold Progress Ring", "show_hold_progress_ring")
  add_anim_checkbox(8, "Show Success Pop", "show_success_pop")
  add_anim_checkbox(9, "Show Success Particles", "show_success_particles")

  animation_layout.addWidget(QLabel("Success Pop Duration (s)"), 10, 0)
  success_pop_spin = QDoubleSpinBox()
  success_pop_spin.setRange(0.0, 1.0)
  success_pop_spin.setSingleStep(0.01)
  success_pop_spin.setDecimals(3)
  success_pop_spin.setValue(float(max(0.0, min(1.0, float(task_config.get("success_pop_duration_s", 0.12))))))
  success_pop_spin.valueChanged.connect(lambda v: task_config.update({"success_pop_duration_s": float(v)}))
  animation_layout.addWidget(success_pop_spin, 10, 1)

  animation_note = QLabel("Set master to OFF for animation-free behavior.")
  animation_layout.addWidget(animation_note, 11, 0, 1, 2)

  layout.addWidget(animation_group)
  sync_table_from_config()

  no_wheel_filter = NoWheelChangeFilter(result)
  result._no_wheel_filter = no_wheel_filter # type: ignore[attr-defined]
  for spinbox in result.findChildren(QSpinBox):
    spinbox.installEventFilter(no_wheel_filter)
  for spinbox in result.findChildren(QDoubleSpinBox):
    spinbox.installEventFilter(no_wheel_filter)
  for combo in result.findChildren(QComboBox):
    combo.installEventFilter(no_wheel_filter)
  for slider in result.findChildren(QSlider):
    slider.installEventFilter(no_wheel_filter)

  return result


async def run(context: TaskContextProtocol) -> TaskResult:
  assert context.widget, "Widget is None; cannot render."

  success_sound = get_sound(os.path.join(os.path.dirname(__file__), "success_clip.wav"))
  fail_sound = get_sound(os.path.join(os.path.dirname(__file__), "failure_clip.wav"))

  task_config = getattr(context, "task_config", context.config["queue"][0])

  def get_persisted_operator_key_state(key: str) -> bool:
    stored = task_config.get("_operator_keys_pressed", {})
    if isinstance(stored, dict):
      return bool(stored.get(key, False))
    return False

  def persist_operator_key_state() -> None:
    task_config["_operator_keys_pressed"] = {
      "left": bool(operator_left_pressed),
      "right": bool(operator_right_pressed),
      "up": bool(operator_up_pressed),
      "down": bool(operator_down_pressed),
    }

  joystick_node = str(task_config.get("joystick_node", "Joystick"))
  up_influence = max(0.0, min(1.0, float(task_config.get("up_influence_pct", 100)) / 100.0))
  down_influence = max(0.0, min(1.0, float(task_config.get("down_influence_pct", 100)) / 100.0))
  left_influence = max(0.0, min(1.0, float(task_config.get("left_influence_pct", 100)) / 100.0))
  right_influence = max(0.0, min(1.0, float(task_config.get("right_influence_pct", 100)) / 100.0))
  control_mode = str(task_config.get("control_mode", "direct"))
  cursor_only_mode = bool(task_config.get("cursor_only_mode", False))
  free_play_end_key = str(task_config.get("free_play_end_key", "space"))
  cumulative_speed = float(task_config.get("cumulative_speed", 0.70))
  zero_drift_mode = bool(task_config.get("zero_drift_mode", True))
  zero_drift_buffer = float(task_config.get("zero_drift_buffer", 0.03))
  direct_range = float(task_config.get("direct_range", 0.45))
  direct_recenter_when_idle = bool(task_config.get("direct_recenter_when_idle", True))
  cursor_diameter_ratio = float(task_config.get("cursor_diameter_ratio", 0.1))
  reset_cursor_each_trial = bool(task_config.get("reset_cursor_each_trial", True))
  require_center_before_trial = bool(task_config.get("require_center_before_trial", False))
  center_gate_radius_ratio = max(0.001, min(1.0, float(task_config.get("center_gate_radius_ratio", 0.15))))
  cursor_color = QColor(*task_config.get("cursor_color", [255, 70, 70]))
  task_region_x = float(task_config.get("task_region_x", 0.5))
  task_region_y = float(task_config.get("task_region_y", 0.270))
  task_region_width = float(task_config.get("task_region_width", 0.5))
  task_region_height = float(task_config.get("task_region_height", 0.67))
  reward_channel = int(task_config.get("reward_channel", 0))
  free_play_active_config_threshold = max(0.0, min(1.0, float(task_config.get("free_play_active_threshold", task_config.get("free_play_reward_threshold", 0.0)))))
  free_play_first_touch_reward_enabled = bool(task_config.get("free_play_first_touch_reward_enabled", False))
  free_play_first_touch_reward_channel = max(0, int(task_config.get("free_play_first_touch_reward_channel", reward_channel)))
  free_play_bout_reward_enabled = bool(task_config.get("free_play_bout_reward_enabled", False))
  free_play_bout_reward_channel = max(0, int(task_config.get("free_play_bout_reward_channel", reward_channel)))
  free_play_bout_cooldown_s = max(0.0, float(task_config.get("free_play_bout_cooldown_s", 1.0)))
  free_play_sustain_reward_enabled = bool(task_config.get("free_play_sustain_reward_enabled", False))
  free_play_sustain_reward_channel = max(0, int(task_config.get("free_play_sustain_reward_channel", reward_channel)))
  free_play_sustain_initial_delay_s = max(0.0, float(task_config.get("free_play_sustain_initial_delay_s", 0.0)))
  free_play_sustain_interval_s = max(0.001, float(task_config.get("free_play_sustain_interval_s", task_config.get("free_play_reward_cooldown_s", 1.0))))
  target_radius_ratio = DEFAULT_TARGET_RADIUS_RATIO
  target_color = QColor(*DEFAULT_TARGET_COLOR)
  hold_time = DEFAULT_TARGET_HOLD_TIME
  trial_timeout = float(task_config.get("trial_timeout", 0.5))
  ignore_idle_trial_failures = bool(task_config.get("ignore_idle_trial_failures", False))
  ignored_idle_sample_clear_threshold = max(0, int(task_config.get("ignored_idle_sample_clear_threshold", 50)))
  max_logged_joystick_samples = max(0, int(task_config.get("max_logged_joystick_samples", DEFAULT_MAX_LOGGED_JOYSTICK_SAMPLES)))
  fail_on_touch_input = bool(task_config.get("fail_on_touch_input", False))
  intertrial_interval = float(task_config.get("intertrial_interval", 1.0))
  configured_targets = task_config.get("targets", [])
  animations_enabled = bool(task_config.get("animations_enabled", False))
  task_animation_enabled = animations_enabled and bool(task_config.get("task_animation_enabled", True))
  target_animation_enabled = animations_enabled and bool(task_config.get("target_animation_enabled", True))
  show_streak_hud = bool(task_config.get("show_streak_hud", True))
  streak_bonus_threshold = max(0, int(task_config.get("streak_bonus_threshold", 0)))
  streak_bonus_reward_count = max(1, int(task_config.get("streak_bonus_reward_count", 1)))
  streak_reset_on_bonus = bool(task_config.get("streak_reset_on_bonus", False))
  show_hold_progress_ring = bool(task_config.get("show_hold_progress_ring", True))
  show_success_pop = bool(task_config.get("show_success_pop", True))
  show_success_particles = bool(task_config.get("show_success_particles", True))
  success_pop_duration_s = max(0.0, min(1.0, float(task_config.get("success_pop_duration_s", 0.12))))
  streak_count = max(0, int(task_config.get("_streak_count", 0)))
  state_indicator_x = max(0, int(task_config.get("state_indicator_x", 30)))
  state_indicator_y = max(0, int(task_config.get("state_indicator_y", 70)))
  session_start = time.perf_counter()
  # Preserve the previous cursor position across task runs when the task is
  # configured for continuous control. This avoids a visible flash back to
  # center before the next joystick sample arrives.
  cursor_x = 0.5 if reset_cursor_each_trial else float(task_config.get("_last_cursor_x", 0.5))
  cursor_y = 0.5 if reset_cursor_each_trial else float(task_config.get("_last_cursor_y", 0.5))
  cursor_x = max(0.0, min(1.0, cursor_x))
  cursor_y = max(0.0, min(1.0, cursor_y))
  target_x = 0.5
  target_y = 0.5

  hold_start: typing.Optional[float] = None
  trial_start = time.perf_counter()
  last_tick = trial_start
  state = "intertrial"
  iti_countdown_start: typing.Optional[float] = None if require_center_before_trial else trial_start
  iti_end = math.inf if require_center_before_trial else trial_start + intertrial_interval
  intertrial_center_ready = False
  current_target_radius_ratio = target_radius_ratio
  current_hold_time = hold_time
  current_target_color = target_color
  current_target_opacity = DEFAULT_TARGET_OPACITY
  current_target_active_color = QColor(*DEFAULT_TARGET_ACTIVE_COLOR)
  current_target_active_opacity = DEFAULT_TARGET_ACTIVE_OPACITY
  current_reward_channel = reward_channel
  TargetSelection = typing.Tuple[int, float, float, float, float, int, QColor, float, QColor, float]
  next_target_preview: typing.Optional[TargetSelection] = None
  free_play_end_requested = False
  analog_joystick_x = 0.0
  analog_joystick_y = 0.0
  operator_left_pressed = get_persisted_operator_key_state("left") if not reset_cursor_each_trial else False
  operator_right_pressed = get_persisted_operator_key_state("right") if not reset_cursor_each_trial else False
  operator_up_pressed = get_persisted_operator_key_state("up") if not reset_cursor_each_trial else False
  operator_down_pressed = get_persisted_operator_key_state("down") if not reset_cursor_each_trial else False
  operator_cursor_latched = False
  joystick_active_this_trial = False
  cursor_inside_target = False
  touch_detected_this_trial = False
  last_touch_pos = QPoint()
  last_touch_time: typing.Optional[float] = None
  hold_progress_ratio = 0.0
  success_pop_start: typing.Optional[float] = None
  success_pop_x = 0.5
  success_pop_y = 0.5
  success_particles: typing.List[typing.Dict[str, typing.Any]] = []
  state_brightness = 0
  current_target_index = -1
  trial_index = 0
  ignored_idle_trial_count = 0
  target_entry_count = 0
  first_movement_time: typing.Optional[float] = None
  first_target_entry_time: typing.Optional[float] = None
  first_hold_start_time: typing.Optional[float] = None
  previous_cursor_inside_target = False
  current_attempt: typing.Optional[typing.Dict[str, typing.Any]] = None
  free_play_was_active = False
  free_play_active_bout_start: typing.Optional[float] = None
  free_play_first_touch_delivered = False
  free_play_last_bout_reward_time: typing.Optional[float] = None
  free_play_last_sustain_reward_time: typing.Optional[float] = None
  free_play_first_touch_reward_count = 0
  free_play_bout_reward_count = 0
  free_play_sustain_reward_count = 0
  free_play_total_reward_count = 0
  free_play_active_bout_count = 0
  free_play_total_active_time_s = 0.0
  free_play_active_threshold = (
    free_play_active_config_threshold
    if free_play_active_config_threshold > 0.0
    else (zero_drift_buffer if zero_drift_mode else 0.02)
  )
  behav_result: typing.Dict[str, typing.Any] = {
    "task": "joystick_intro",
    "control_mode": control_mode,
    "cursor_only_mode": cursor_only_mode,
    "require_center_before_trial": require_center_before_trial,
    "center_gate_radius_ratio": center_gate_radius_ratio,
    "ignored_idle_sample_clear_threshold": ignored_idle_sample_clear_threshold,
    "max_logged_joystick_samples": max_logged_joystick_samples,
    "ignored_idle_sample_clear_events": [],
    "fail_on_touch_input": fail_on_touch_input,
    "trial_attempt_count": 0,
    "attempts": [],
    "joystick_samples": [],
    "joystick_sample_count": 0,
    "joystick_samples_dropped": 0,
    "joystick_samples_kept": 0,
    "session_start_perf_counter": session_start,
    "final_outcome": None,
  }
  context.behav_result = behav_result

  task_region_width = max(0.05, min(1.0, task_region_width))
  task_region_height = max(0.05, min(1.0, task_region_height))
  task_region_x = max(0.0, min(1.0, task_region_x))
  task_region_y = max(0.0, min(1.0, task_region_y))

  def region_bounds_ratios() -> typing.Tuple[float, float, float, float]:
    left = task_region_x - (task_region_width / 2.0)
    top = task_region_y - (task_region_height / 2.0)
    left = max(0.0, min(1.0 - task_region_width, left))
    top = max(0.0, min(1.0 - task_region_height, top))
    return left, top, task_region_width, task_region_height

  def to_region_pixels(local_x: float, local_y: float, width_px: int, height_px: int) -> typing.Tuple[int, int]:
    region_left, region_top, region_w, region_h = region_bounds_ratios()
    left_px = int(region_left * width_px)
    top_px = int(region_top * height_px)
    region_w_px = max(1, int(region_w * width_px))
    region_h_px = max(1, int(region_h * height_px))
    px = int(left_px + local_x * region_w_px)
    py = int(top_px + (1.0 - local_y) * region_h_px)
    return px, py

  def on_key_release(event: typing.Any) -> None:
    nonlocal free_play_end_requested
    nonlocal operator_left_pressed
    nonlocal operator_right_pressed
    nonlocal operator_up_pressed
    nonlocal operator_down_pressed
    key = event.key()
    if key == Qt.Key.Key_Left:
      operator_left_pressed = False
    elif key == Qt.Key.Key_Right:
      operator_right_pressed = False
    elif key == Qt.Key.Key_Up:
      operator_up_pressed = False
    elif key == Qt.Key.Key_Down:
      operator_down_pressed = False
    persist_operator_key_state()
    if free_play_end_key == "q":
      free_play_end_requested = (key == Qt.Key.Key_Q)
    elif free_play_end_key == "enter":
      free_play_end_requested = (key == Qt.Key.Key_Return or key == Qt.Key.Key_Enter)
    else:
      free_play_end_requested = (key == Qt.Key.Key_Space)

  def on_key_press(event: QKeyEvent) -> None:
    nonlocal free_play_end_requested
    nonlocal operator_left_pressed
    nonlocal operator_right_pressed
    nonlocal operator_up_pressed
    nonlocal operator_down_pressed
    if event.isAutoRepeat():
      return
    key = event.key()
    if key == Qt.Key.Key_Left:
      operator_left_pressed = True
    elif key == Qt.Key.Key_Right:
      operator_right_pressed = True
    elif key == Qt.Key.Key_Up:
      operator_up_pressed = True
    elif key == Qt.Key.Key_Down:
      operator_down_pressed = True
    persist_operator_key_state()

  def touch_handler(cursor: QPoint) -> None:
    nonlocal touch_detected_this_trial
    nonlocal last_touch_pos
    nonlocal last_touch_time
    if cursor.x() < 0:
      return
    touch_detected_this_trial = True
    last_touch_pos = cursor
    last_touch_time = time.perf_counter()

  def append_joystick_sample(sample_time: float, sample_x: float, sample_y: float) -> None:
    behav_result["joystick_sample_count"] += 1
    if max_logged_joystick_samples <= 0:
      behav_result["joystick_samples_dropped"] += 1
      behav_result["joystick_samples_kept"] = 0
      return

    samples = behav_result["joystick_samples"]
    samples.append({
      "time_perf_counter": sample_time,
      "time_since_session_start_s": max(0.0, sample_time - session_start),
      "x": sample_x,
      "y": sample_y,
    })
    if len(samples) > max_logged_joystick_samples:
      overflow = len(samples) - max_logged_joystick_samples
      del samples[:overflow]
      behav_result["joystick_samples_dropped"] += overflow
    behav_result["joystick_samples_kept"] = len(samples)

  async def analog_processor(stream: typing.Any) -> None:
    nonlocal analog_joystick_x, analog_joystick_y
    try:
      async for message in stream:
        sample_received_at = time.perf_counter()
        if len(message.spans) >= 2:
          x_span = message.spans[0]
          y_span = message.spans[1]
          x_values = message.data[x_span.begin:x_span.end]
          y_values = message.data[y_span.begin:y_span.end]
          sample_count = min(len(x_values), len(y_values))
          if sample_count <= 0:
            continue
          x_interval_s = 0.0
          y_interval_s = 0.0
          if len(message.sample_intervals) >= 1:
            x_interval_s = max(0.0, float(message.sample_intervals[0]) / 1e9)
          if len(message.sample_intervals) >= 2:
            y_interval_s = max(0.0, float(message.sample_intervals[1]) / 1e9)
          sample_interval_s = x_interval_s or y_interval_s
          start_time = sample_received_at - sample_interval_s * max(0, sample_count - 1)
          for i in range(sample_count):
            sample_time = start_time + sample_interval_s * i if sample_interval_s > 0.0 else sample_received_at
            sample_x = float(x_values[i])
            sample_y = float(y_values[i])
            append_joystick_sample(sample_time, sample_x, sample_y)
            analog_joystick_x = sample_x
            analog_joystick_y = sample_y
        elif len(message.data) >= 2:
          analog_joystick_x = float(message.data[0])
          analog_joystick_y = float(message.data[1])
          append_joystick_sample(sample_received_at, analog_joystick_x, analog_joystick_y)
    finally:
      stream.cancel()

  def get_operator_joystick() -> typing.Tuple[float, float]:
    x = float(operator_right_pressed) - float(operator_left_pressed)
    y = float(operator_up_pressed) - float(operator_down_pressed)
    return (
      max(-1.0, min(1.0, x * KEYBOARD_JOYSTICK_MAGNITUDE)),
      max(-1.0, min(1.0, y * KEYBOARD_JOYSTICK_MAGNITUDE)),
    )

  async def deliver_reward(channel: int) -> None:
    # The channel still maps to a base pulse duration (ms) via the shared reward
    # schedule, exactly as every other task expects. We then apply a continuous
    # multiplier local to this task so reward can be ramped in fine steps without
    # jumping a whole channel. reward_scale defaults to 1.0 (identical behavior).
    base_ms = float(context.get_reward(channel))
    reward_scale = clamp_float(task_config.get("reward_scale", 1.0), 0.0, 100.0, 1.0)
    on_time_ms = int(round(base_ms * reward_scale))
    if on_time_ms <= 0:
      LOGGER.info(
        "Reward skipped: channel=%d base_ms=%.1f scale=%.3f -> %d ms",
        channel, base_ms, reward_scale, on_time_ms,
      )
      return
    signal = thalamus_pb2.AnalogResponse(
      data=[5, 0],
      spans=[thalamus_pb2.Span(begin=0, end=2, name='Reward')],
      sample_intervals=[1_000_000 * on_time_ms],
    )
    LOGGER.info(
      "Delivering reward channel=%d base_ms=%.1f scale=%.3f duration_ms=%d",
      channel, base_ms, reward_scale, on_time_ms,
    )
    await context.inject_analog('Reward', signal)

  async def deliver_reward_repeats(channel: int, repeats: int) -> None:
    for i in range(max(0, repeats)):
      await deliver_reward(channel)
      if i < repeats - 1:
        await context.sleep(datetime.timedelta(seconds=0.05))

  def reset_attempt_tracking(now: float) -> None:
    nonlocal target_entry_count
    nonlocal first_movement_time
    nonlocal first_target_entry_time
    nonlocal first_hold_start_time
    nonlocal previous_cursor_inside_target
    nonlocal current_attempt
    nonlocal trial_index

    trial_index += 1
    target_entry_count = 0
    first_movement_time = None
    first_target_entry_time = None
    first_hold_start_time = None
    previous_cursor_inside_target = False
    current_attempt = {
      "attempt_index": trial_index,
      "start_time_perf_counter": now,
      "control_mode": control_mode,
      "cursor_only_mode": cursor_only_mode,
      "target_index": None,
      "target_position": None,
      "target_radius_ratio": None,
      "hold_time_s": None,
      "reward_channel": None,
      "target_color_rgb": None,
      "target_opacity": None,
      "target_active_color_rgb": None,
      "target_active_opacity": None,
      "events": [],
      "joystick_active": False,
      "target_entry_count": 0,
      "outcome": None,
      "failure_reason": None,
    }

  def append_event(name: str, event_time: float, **extra: typing.Any) -> None:
    if current_attempt is None:
      return
    event = {
      "name": name,
      "time_perf_counter": event_time,
      "time_since_attempt_start_s": max(0.0, event_time - float(current_attempt["start_time_perf_counter"])),
    }
    event.update(extra)
    current_attempt["events"].append(event)

  def finalize_attempt(outcome: str, now: float, failure_reason: typing.Optional[str] = None) -> None:
    if current_attempt is None:
      return
    current_attempt["outcome"] = outcome
    current_attempt["failure_reason"] = failure_reason
    current_attempt["joystick_active"] = joystick_active_this_trial
    current_attempt["target_entry_count"] = target_entry_count
    current_attempt["end_time_perf_counter"] = now
    current_attempt["duration_s"] = max(0.0, now - float(current_attempt["start_time_perf_counter"]))
    current_attempt["first_movement_time_s"] = None if first_movement_time is None else max(0.0, first_movement_time - float(current_attempt["start_time_perf_counter"]))
    current_attempt["first_target_entry_time_s"] = None if first_target_entry_time is None else max(0.0, first_target_entry_time - float(current_attempt["start_time_perf_counter"]))
    current_attempt["first_hold_start_time_s"] = None if first_hold_start_time is None else max(0.0, first_hold_start_time - float(current_attempt["start_time_perf_counter"]))
    current_attempt["success_time_s"] = current_attempt["duration_s"] if outcome == "success" else None
    behav_result["attempts"].append(current_attempt)
    behav_result["trial_attempt_count"] = len(behav_result["attempts"])
    behav_result["final_outcome"] = outcome
    behav_result["final_attempt"] = current_attempt
    context.behav_result = behav_result

  def clear_joystick_samples_for_prolonged_idle(now: float) -> None:
    cleared_count = len(behav_result["joystick_samples"])
    if cleared_count <= 0:
      return
    clear_events = behav_result.get("ignored_idle_sample_clear_events", [])
    if not isinstance(clear_events, list):
      clear_events = []
      behav_result["ignored_idle_sample_clear_events"] = clear_events
    clear_events.append({
      "time_perf_counter": now,
      "time_since_session_start_s": max(0.0, now - session_start),
      "ignored_idle_trial_count": ignored_idle_trial_count,
      "cleared_sample_count": cleared_count,
    })
    behav_result["joystick_samples"].clear()
    behav_result["joystick_samples_dropped"] += cleared_count
    behav_result["joystick_samples_kept"] = 0
    context.behav_result = behav_result

  async def fail_for_touch_input(now: float) -> TaskResult:
    nonlocal streak_count
    touch_time = now if last_touch_time is None else last_touch_time
    append_event(
      "touch_input_fail",
      now,
      touch_x=last_touch_pos.x(),
      touch_y=last_touch_pos.y(),
      touch_time_perf_counter=touch_time,
      touch_time_since_session_start_s=max(0.0, touch_time - session_start),
    )
    streak_count = 0
    task_config["_streak_count"] = 0
    finalize_attempt("fail", now, failure_reason="touch_input")
    fail_sound.play()
    await context.log("BehavState=fail")
    return TaskResult(success=False)

  def reset_intertrial_gate(now: float) -> None:
    nonlocal iti_countdown_start
    nonlocal iti_end
    nonlocal intertrial_center_ready

    if require_center_before_trial:
      iti_countdown_start = None
      iti_end = math.inf
      intertrial_center_ready = False
    else:
      iti_countdown_start = now
      iti_end = now + intertrial_interval
      intertrial_center_ready = True

  def update_intertrial_gate(now: float, cursor_centered: bool) -> None:
    nonlocal iti_countdown_start
    nonlocal iti_end
    nonlocal intertrial_center_ready

    if not require_center_before_trial:
      return
    if cursor_centered:
      if not intertrial_center_ready:
        intertrial_center_ready = True
        iti_countdown_start = now
        iti_end = now + intertrial_interval
      return
    intertrial_center_ready = False
    iti_countdown_start = None
    iti_end = math.inf

  def parse_target_color(target: typing.Dict[str, typing.Any], key: str, default_color: QColor) -> QColor:
    raw_color = target.get(key, [default_color.red(), default_color.green(), default_color.blue()])
    rgb = normalize_rgb(raw_color, [default_color.red(), default_color.green(), default_color.blue()])
    return QColor(rgb[0], rgb[1], rgb[2])

  def place_target() -> TargetSelection:
    # Intentionally random-only: `target_schedule` (sequence/center-out) is
    # honored by the Rust executor (rust/joystick_task state.rs place_target);
    # this pure-Python path stays a uniform random draw.
    enabled_targets = []
    for index, target in enumerate(configured_targets):
      if not isinstance(target, dict):
        continue
      if not bool(target.get("enabled", True)):
        continue
      tx = max(0.0, min(1.0, float(target.get("x_norm", 0.75))))
      ty = max(0.0, min(1.0, float(target.get("y_norm", 0.50))))
      tr = max(0.01, min(0.5, float(target.get("radius_ratio", target_radius_ratio))))
      th = max(0.01, min(10.0, float(target.get("hold_time", hold_time))))
      rc = max(0, int(target.get("reward_channel", reward_channel)))
      tc = parse_target_color(target, "target_color", target_color)
      target_opacity = clamp_float(target.get("target_opacity", DEFAULT_TARGET_OPACITY), 0.0, 1.0, DEFAULT_TARGET_OPACITY)
      active_color = parse_target_color(target, "target_active_color", QColor(*DEFAULT_TARGET_ACTIVE_COLOR))
      active_opacity = clamp_float(
        target.get("target_active_opacity", DEFAULT_TARGET_ACTIVE_OPACITY),
        0.0,
        1.0,
        DEFAULT_TARGET_ACTIVE_OPACITY,
      )
      enabled_targets.append((index, tx, ty, tr, th, rc, tc, target_opacity, active_color, active_opacity))
    if enabled_targets:
      return random.choice(enabled_targets)
    return (
      -1,
      0.75,
      0.50,
      target_radius_ratio,
      hold_time,
      reward_channel,
      target_color,
      DEFAULT_TARGET_OPACITY,
      QColor(*DEFAULT_TARGET_ACTIVE_COLOR),
      DEFAULT_TARGET_ACTIVE_OPACITY,
    )

  def ensure_next_target_preview() -> None:
    nonlocal next_target_preview
    if next_target_preview is None:
      next_target_preview = place_target()

  def consume_next_target() -> TargetSelection:
    nonlocal next_target_preview
    ensure_next_target_preview()
    assert next_target_preview is not None
    selected_target = next_target_preview
    next_target_preview = place_target()
    return selected_target

  def apply_direction_influence(raw_jx: float, raw_jy: float) -> typing.Tuple[float, float]:
    jx = raw_jx
    jy = raw_jy
    if jx > 0.0:
      jx *= right_influence
    elif jx < 0.0:
      jx *= left_influence
    if jy > 0.0:
      jy *= up_influence
    elif jy < 0.0:
      jy *= down_influence
    return jx, jy

  async def trigger_free_play_reward(
    event_name: str,
    now: float,
    channel: int,
    reward_kind: str,
    analog_magnitude: float,
  ) -> None:
    nonlocal free_play_first_touch_reward_count
    nonlocal free_play_bout_reward_count
    nonlocal free_play_sustain_reward_count
    nonlocal free_play_total_reward_count

    await deliver_reward_repeats(channel, 1)
    success_sound.play()
    free_play_total_reward_count += 1
    if reward_kind == "first_touch":
      free_play_first_touch_reward_count += 1
    elif reward_kind == "bout":
      free_play_bout_reward_count += 1
    elif reward_kind == "sustain":
      free_play_sustain_reward_count += 1

    if current_attempt is not None:
      current_attempt["free_play_first_touch_reward_count"] = free_play_first_touch_reward_count
      current_attempt["free_play_bout_reward_count"] = free_play_bout_reward_count
      current_attempt["free_play_sustain_reward_count"] = free_play_sustain_reward_count
      current_attempt["free_play_total_reward_count"] = free_play_total_reward_count
    append_event(
      event_name,
      now,
      reward_count=1,
      reward_channel=channel,
      reward_kind=reward_kind,
      total_free_play_reward_count=free_play_total_reward_count,
      joystick_x=analog_joystick_x,
      joystick_y=analog_joystick_y,
      joystick_magnitude=analog_magnitude,
    )

  def get_current_target_name() -> str:
    if 0 <= current_target_index < len(configured_targets):
      target = configured_targets[current_target_index]
      if isinstance(target, dict):
        name = str(target.get("name", "")).strip()
        if name:
          return name
    if current_target_index >= 0:
      return f"Target {current_target_index + 1}"
    return "None"

  def get_target_name(index: int) -> str:
    if 0 <= index < len(configured_targets):
      target = configured_targets[index]
      if isinstance(target, dict):
        name = str(target.get("name", "")).strip()
        if name:
          return name
    if index >= 0:
      return f"Target {index + 1}"
    return "None"

  def get_next_target_label() -> str:
    if next_target_preview is None:
      return "None"
    return get_target_name(next_target_preview[0])

  def draw_operator_hud(painter: CanvasPainterProtocol, width: int, height: int) -> None:
    original_font = painter.font()
    hud_font = QFont(original_font)
    hud_font.setPointSize(max(24, original_font.pointSize() + 12))
    painter.setFont(hud_font)
    size_percent = 100.0 * current_target_radius_ratio
    lines = [
      f"Target: {get_current_target_name()}",
      f"Size: {size_percent:.1f}% region radius",
      f"Reward Channel: {current_reward_channel}",
      f"Next Target: {get_next_target_label()}",
    ]
    margin = 14
    line_spacing = 6
    metrics = painter.fontMetrics()
    line_height = metrics.height()
    block_height = len(lines) * line_height + max(0, len(lines) - 1) * line_spacing
    y = margin
    for index, line in enumerate(lines):
      line_top = y + index * (line_height + line_spacing)
      painter.drawText(
        QRect(margin, line_top, max(1, width - 2 * margin), line_height),
        int(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignTop),
        line,
      )
    painter.setFont(original_font)

  def spawn_success_particles(start_time: float) -> None:
    success_particles.clear()
    duration_s = max(0.18, min(0.45, success_pop_duration_s if success_pop_duration_s > 0.0 else 0.26))
    active_rgb = [
      current_target_active_color.red(),
      current_target_active_color.green(),
      current_target_active_color.blue(),
    ]
    static_rgb = [
      current_target_color.red(),
      current_target_color.green(),
      current_target_color.blue(),
    ]
    particle_count = 16
    for i in range(particle_count):
      base_angle = (2.0 * math.pi * i) / float(particle_count)
      angle = base_angle + random.uniform(-0.12, 0.12)
      success_particles.append({
        "start": start_time,
        "duration": duration_s * random.uniform(0.82, 1.08),
        "angle": angle,
        "travel": random.uniform(1.15, 2.15),
        "size": random.uniform(0.055, 0.095),
        "color": active_rgb if i % 3 else static_rgb,
      })

  def renderer(painter: CanvasPainterProtocol) -> None:
    nonlocal success_pop_start
    w = context.widget.width()
    h = context.widget.height()
    region_left, region_top, region_w, region_h = region_bounds_ratios()
    left_px = int(region_left * w)
    top_px = int(region_top * h)
    region_w_px = max(1, int(region_w * w))
    region_h_px = max(1, int(region_h * h))
    min_dim = min(region_w_px, region_h_px)

    cursor_diameter_px = int(cursor_diameter_ratio * min_dim)
    target_radius_px = int(current_target_radius_ratio * min_dim)
    cx, cy = to_region_pixels(cursor_x, cursor_y, w, h)
    tx, ty = to_region_pixels(target_x, target_y, w, h)

    painter.setPen(QPen(QColor(120, 120, 120), 1))
    painter.setBrush(Qt.BrushStyle.NoBrush)
    painter.drawRect(left_px, top_px, region_w_px, region_h_px)

    if (not cursor_only_mode) and state == "start_on":
      if cursor_inside_target:
        draw_target_color = QColor(current_target_active_color)
        draw_target_color.setAlpha(int(round(255 * current_target_active_opacity)))
      else:
        draw_target_color = QColor(current_target_color)
        draw_target_color.setAlpha(int(round(255 * current_target_opacity)))
      painter.setPen(QPen(draw_target_color, 1))
      painter.setBrush(draw_target_color)
      painter.drawEllipse(tx - target_radius_px, ty - target_radius_px, 2 * target_radius_px, 2 * target_radius_px)
      if target_animation_enabled and show_hold_progress_ring and hold_progress_ratio > 0.0:
        progress = max(0.0, min(1.0, hold_progress_ratio))
        eased_progress = progress * progress * (3.0 - 2.0 * progress)
        ring_radius = target_radius_px + max(5, int(0.022 * min_dim))
        ring_width = max(4, int(max(0.014 * min_dim, target_radius_px * 0.18)))
        diameter = 2 * ring_radius

        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(QColor(0, 0, 0, 150), ring_width + 4))
        painter.drawEllipse(tx - ring_radius, ty - ring_radius, diameter, diameter)
        painter.setPen(QPen(QColor(255, 255, 255, 92), max(1, ring_width + 1)))
        painter.drawEllipse(tx - ring_radius, ty - ring_radius, diameter, diameter)
        painter.setPen(QPen(QColor(0, 0, 0, 110), max(1, ring_width - 2)))
        painter.drawEllipse(tx - ring_radius, ty - ring_radius, diameter, diameter)

        progress_color = QColor(current_target_active_color)
        progress_color.setAlpha(245)
        painter.setPen(QPen(progress_color, ring_width))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawArc(
          tx - ring_radius,
          ty - ring_radius,
          diameter,
          diameter,
          90 * 16,
          -int(360 * 16 * eased_progress),
        )

    if target_animation_enabled and show_success_pop and success_pop_start is not None and success_pop_duration_s > 0.0:
      elapsed = time.perf_counter() - success_pop_start
      if elapsed <= success_pop_duration_s:
        progress = max(0.0, min(1.0, elapsed / success_pop_duration_s))
        pop_center_x, pop_center_y = to_region_pixels(success_pop_x, success_pop_y, w, h)
        pop_radius = int(target_radius_px * (1.0 + 0.8 * progress))
        alpha = int(255 * (1.0 - progress))
        pop_width = max(1, int(0.006 * min_dim))
        painter.setPen(QPen(QColor(current_target_color.red(), current_target_color.green(), current_target_color.blue(), alpha), pop_width))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(pop_center_x - pop_radius, pop_center_y - pop_radius, 2 * pop_radius, 2 * pop_radius)
      else:
        success_pop_start = None

    if target_animation_enabled and show_success_particles and success_particles:
      active_particles: typing.List[typing.Dict[str, typing.Any]] = []
      now_perf = time.perf_counter()
      particle_center_x, particle_center_y = to_region_pixels(success_pop_x, success_pop_y, w, h)
      for particle in success_particles:
        duration = max(0.001, float(particle.get("duration", 0.26)))
        elapsed = now_perf - float(particle.get("start", now_perf))
        if elapsed > duration:
          continue
        active_particles.append(particle)
        progress = max(0.0, min(1.0, elapsed / duration))
        eased = 1.0 - pow(1.0 - progress, 3.0)
        alpha = int(230 * pow(1.0 - progress, 1.4))
        angle = float(particle.get("angle", 0.0))
        travel_px = float(particle.get("travel", 1.5)) * target_radius_px * eased
        px = particle_center_x + int(math.cos(angle) * travel_px)
        py = particle_center_y + int(math.sin(angle) * travel_px)
        size_px = max(2, int(target_radius_px * float(particle.get("size", 0.07)) * (1.2 - 0.45 * progress)))
        rgb = particle.get("color", DEFAULT_TARGET_ACTIVE_COLOR)
        particle_color = QColor(int(rgb[0]), int(rgb[1]), int(rgb[2]), alpha)
        painter.setPen(QPen(QColor(255, 255, 255, min(180, alpha)), max(1, size_px // 2)))
        painter.drawLine(
          particle_center_x + int(math.cos(angle) * travel_px * 0.72),
          particle_center_y + int(math.sin(angle) * travel_px * 0.72),
          px,
          py,
        )
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(particle_color)
        painter.drawEllipse(px - size_px, py - size_px, 2 * size_px, 2 * size_px)
      success_particles[:] = active_particles

    r = cursor_diameter_px // 2
    painter.setPen(QPen(cursor_color, 1))
    painter.setBrush(cursor_color)
    painter.drawEllipse(cx - r, cy - r, cursor_diameter_px, cursor_diameter_px)

    painter.setPen(QPen(QColor(255, 255, 255), 1))
    status_text = f"Mode: {control_mode}"
    if cursor_only_mode:
      status_text += f"  Free Play (press {free_play_end_key.upper()} to end)"
    painter.drawText(10, 20, status_text)
    if task_animation_enabled and show_streak_hud and not cursor_only_mode:
      painter.drawText(10, 40, f"Streak: {streak_count}")
      if streak_bonus_threshold > 0:
        progress_step = streak_count % streak_bonus_threshold
        if progress_step == 0 and streak_count > 0:
          progress_step = streak_bonus_threshold
        progress_ratio = progress_step / streak_bonus_threshold
        bar_w = 170
        bar_h = 8
        x0 = 10
        y0 = 48
        painter.setPen(QPen(QColor(180, 180, 180), 1))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRect(x0, y0, bar_w, bar_h)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(100, 210, 120))
        painter.drawRect(x0 + 1, y0 + 1, int((bar_w - 2) * progress_ratio), bar_h - 2)
        painter.setPen(QPen(QColor(255, 255, 255), 1))
        painter.drawText(10, 68, f"Bonus @ {streak_bonus_threshold}")

    state_color = QColor(state_brightness, state_brightness, state_brightness)
    state_width = 70
    painter.fillRect(
      w - state_width - state_indicator_x,
      h - state_width - state_indicator_y,
      state_width,
      state_width,
      state_color,
    )

    with painter.masked(RenderOutput.OPERATOR):
      painter.setPen(QPen(QColor(255, 255, 255), 1))
      draw_operator_hud(painter, w, h)

  context.widget.renderer = renderer
  context.widget.key_release_handler = on_key_release
  context.widget.key_press_handler = on_key_press
  context.widget.touch_listener = touch_handler
  context.widget.setFocus()

  channel = context.get_channel('localhost:50050')
  stub = thalamus_pb2_grpc.ThalamusStub(channel)
  request = thalamus_pb2.AnalogRequest(
    node=thalamus_pb2.NodeSelector(name=joystick_node),
    channel_names=['X', 'Y'],
  )
  stream = stub.analog(request)
  analog_task = asyncio.get_event_loop().create_task(analog_processor(stream))

  try:
    if not cursor_only_mode:
      ensure_next_target_preview()
    if cursor_only_mode:
      reset_attempt_tracking(session_start)
      if current_attempt is not None:
        current_attempt["free_play_active_threshold"] = free_play_active_threshold
        current_attempt["free_play_first_touch_reward_enabled"] = free_play_first_touch_reward_enabled
        current_attempt["free_play_first_touch_reward_channel"] = free_play_first_touch_reward_channel
        current_attempt["free_play_bout_reward_enabled"] = free_play_bout_reward_enabled
        current_attempt["free_play_bout_reward_channel"] = free_play_bout_reward_channel
        current_attempt["free_play_bout_cooldown_s"] = free_play_bout_cooldown_s
        current_attempt["free_play_sustain_reward_enabled"] = free_play_sustain_reward_enabled
        current_attempt["free_play_sustain_reward_channel"] = free_play_sustain_reward_channel
        current_attempt["free_play_sustain_initial_delay_s"] = free_play_sustain_initial_delay_s
        current_attempt["free_play_sustain_interval_s"] = free_play_sustain_interval_s
        current_attempt["free_play_first_touch_reward_count"] = 0
        current_attempt["free_play_bout_reward_count"] = 0
        current_attempt["free_play_sustain_reward_count"] = 0
        current_attempt["free_play_total_reward_count"] = 0
        current_attempt["free_play_active_bout_count"] = 0
        current_attempt["free_play_total_active_time_s"] = 0.0
      append_event("free_play_start", session_start)
    await context.log("BehavState=intertrial")
    while True:
      now = time.perf_counter()
      dt = max(0.0, min(0.05, now - last_tick))
      last_tick = now

      operator_jx, operator_jy = get_operator_joystick()
      operator_override_active = operator_jx != 0.0 or operator_jy != 0.0
      analog_magnitude = math.hypot(analog_joystick_x, analog_joystick_y)
      analog_active = analog_magnitude >= (zero_drift_buffer if zero_drift_mode else 0.02)
      if operator_override_active:
        jx, jy = apply_direction_influence(operator_jx, operator_jy)
        cursor_x += jx * OPERATOR_KEYBOARD_CURSOR_SPEED * dt
        cursor_y += jy * OPERATOR_KEYBOARD_CURSOR_SPEED * dt
        operator_cursor_latched = True
      elif operator_cursor_latched and not analog_active:
        jx = 0.0
        jy = 0.0
      elif control_mode == "direct":
        jx, jy = apply_direction_influence(analog_joystick_x, analog_joystick_y)
        if analog_active or direct_recenter_when_idle:
          cursor_x = 0.5 + jx * direct_range
          cursor_y = 0.5 + jy * direct_range
        operator_cursor_latched = False
      else:
        jx, jy = apply_direction_influence(analog_joystick_x, analog_joystick_y)
        if zero_drift_mode:
          # Radial deadband to suppress small spring-back offsets near center.
          if math.hypot(jx, jy) < zero_drift_buffer:
            jx = 0.0
            jy = 0.0
        cursor_x += jx * cumulative_speed * dt
        cursor_y += jy * cumulative_speed * dt
        operator_cursor_latched = False

      joystick_motion_threshold = zero_drift_buffer if zero_drift_mode else 0.02
      joystick_is_active = math.hypot(jx, jy) >= joystick_motion_threshold

      cursor_x = max(0.0, min(1.0, cursor_x))
      cursor_y = max(0.0, min(1.0, cursor_y))

      w = context.widget.width()
      h = context.widget.height()
      _, _, region_w, region_h = region_bounds_ratios()
      region_w_px = max(1, int(region_w * w))
      region_h_px = max(1, int(region_h * h))
      min_dim = min(region_w_px, region_h_px)
      target_radius_px = int(current_target_radius_ratio * min_dim)
      cursor_px_x, cursor_px_y = to_region_pixels(cursor_x, cursor_y, w, h)
      target_px_x, target_px_y = to_region_pixels(target_x, target_y, w, h)
      center_px_x, center_px_y = to_region_pixels(0.5, 0.5, w, h)
      center_gate_radius_px = max(1, int(center_gate_radius_ratio * min_dim))
      cursor_centered_for_next_trial = (
        math.hypot(cursor_px_x - center_px_x, cursor_px_y - center_px_y)
        <= center_gate_radius_px
      )
      cursor_inside_target = False
      hold_progress_ratio = 0.0

      if cursor_only_mode:
        if fail_on_touch_input and touch_detected_this_trial:
          return await fail_for_touch_input(now)

        free_play_is_active = analog_magnitude >= free_play_active_threshold
        free_play_started = free_play_is_active and not free_play_was_active
        free_play_ended = (not free_play_is_active) and free_play_was_active

        if free_play_started:
          free_play_active_bout_start = now
          free_play_active_bout_count += 1
          free_play_last_sustain_reward_time = None
          if current_attempt is not None:
            current_attempt["free_play_active_bout_count"] = free_play_active_bout_count
          append_event(
            "free_play_active_start",
            now,
            active_bout_count=free_play_active_bout_count,
            joystick_x=analog_joystick_x,
            joystick_y=analog_joystick_y,
            joystick_magnitude=analog_magnitude,
          )

        if free_play_is_active and not joystick_active_this_trial:
          first_movement_time = now
          append_event(
            "first_joystick_movement",
            now,
            joystick_x=analog_joystick_x,
            joystick_y=analog_joystick_y,
            joystick_magnitude=analog_magnitude,
          )
        if free_play_is_active:
          joystick_active_this_trial = True
          if current_attempt is not None:
            current_attempt["joystick_active"] = True

        if free_play_started and free_play_first_touch_reward_enabled and not free_play_first_touch_delivered:
          await trigger_free_play_reward(
            "free_play_first_touch_reward_triggered",
            now,
            free_play_first_touch_reward_channel,
            "first_touch",
            analog_magnitude,
          )
          free_play_first_touch_delivered = True

        bout_cooldown_elapsed = (
          free_play_last_bout_reward_time is None
          or now - free_play_last_bout_reward_time >= free_play_bout_cooldown_s
        )
        if free_play_started and free_play_bout_reward_enabled and bout_cooldown_elapsed:
          await trigger_free_play_reward(
            "free_play_bout_reward_triggered",
            now,
            free_play_bout_reward_channel,
            "bout",
            analog_magnitude,
          )
          free_play_last_bout_reward_time = now

        if free_play_is_active and free_play_sustain_reward_enabled and free_play_active_bout_start is not None:
          active_duration_s = max(0.0, now - free_play_active_bout_start)
          sustain_delay_elapsed = active_duration_s >= free_play_sustain_initial_delay_s
          sustain_interval_elapsed = (
            free_play_last_sustain_reward_time is None
            or now - free_play_last_sustain_reward_time >= free_play_sustain_interval_s
          )
          if sustain_delay_elapsed and sustain_interval_elapsed:
            await trigger_free_play_reward(
              "free_play_sustain_reward_triggered",
              now,
              free_play_sustain_reward_channel,
              "sustain",
              analog_magnitude,
            )
            free_play_last_sustain_reward_time = now

        if free_play_ended:
          bout_duration_s = 0.0 if free_play_active_bout_start is None else max(0.0, now - free_play_active_bout_start)
          free_play_total_active_time_s += bout_duration_s
          if current_attempt is not None:
            current_attempt["free_play_total_active_time_s"] = free_play_total_active_time_s
          append_event(
            "free_play_active_end",
            now,
            active_bout_count=free_play_active_bout_count,
            active_bout_duration_s=bout_duration_s,
            total_active_time_s=free_play_total_active_time_s,
            joystick_x=analog_joystick_x,
            joystick_y=analog_joystick_y,
            joystick_magnitude=analog_magnitude,
          )
          free_play_active_bout_start = None
          free_play_last_sustain_reward_time = None
        free_play_was_active = free_play_is_active

        if free_play_end_requested:
          if free_play_is_active and free_play_active_bout_start is not None:
            bout_duration_s = max(0.0, now - free_play_active_bout_start)
            free_play_total_active_time_s += bout_duration_s
            if current_attempt is not None:
              current_attempt["free_play_total_active_time_s"] = free_play_total_active_time_s
            append_event(
              "free_play_active_end",
              now,
              active_bout_count=free_play_active_bout_count,
              active_bout_duration_s=bout_duration_s,
              total_active_time_s=free_play_total_active_time_s,
              joystick_x=analog_joystick_x,
              joystick_y=analog_joystick_y,
              joystick_magnitude=analog_magnitude,
            )
            free_play_active_bout_start = None
          append_event("free_play_end_requested", now)
          finalize_attempt("success", now)
          success_sound.play()
          await context.log("BehavState=success")
          return TaskResult(success=True)
      elif state == "intertrial":
        update_intertrial_gate(now, cursor_centered_for_next_trial)
        if now >= iti_end:
          (
            current_target_index,
            target_x,
            target_y,
            current_target_radius_ratio,
            current_hold_time,
            current_reward_channel,
            current_target_color,
            current_target_opacity,
            current_target_active_color,
            current_target_active_opacity,
          ) = consume_next_target()
          hold_start = None
          trial_start = now
          reset_attempt_tracking(now)
          joystick_active_this_trial = False
          touch_detected_this_trial = False
          last_touch_pos = QPoint()
          last_touch_time = None
          state = "start_on"
          state_brightness = toggle_brightness(state_brightness)
          if current_attempt is not None:
            current_attempt["target_index"] = current_target_index
            current_attempt["target_position"] = {"x_norm": target_x, "y_norm": target_y}
            current_attempt["target_radius_ratio"] = current_target_radius_ratio
            current_attempt["hold_time_s"] = current_hold_time
            current_attempt["reward_channel"] = current_reward_channel
            current_attempt["target_color_rgb"] = [
              current_target_color.red(),
              current_target_color.green(),
              current_target_color.blue(),
            ]
            current_attempt["target_opacity"] = current_target_opacity
            current_attempt["target_active_color_rgb"] = [
              current_target_active_color.red(),
              current_target_active_color.green(),
              current_target_active_color.blue(),
            ]
            current_attempt["target_active_opacity"] = current_target_active_opacity
          append_event(
            "target_on",
            now,
            target_index=current_target_index,
            target_x=target_x,
            target_y=target_y,
            target_radius_ratio=current_target_radius_ratio,
            hold_time_s=current_hold_time,
            reward_channel=current_reward_channel,
            target_color_rgb=[
              current_target_color.red(),
              current_target_color.green(),
              current_target_color.blue(),
            ],
            target_opacity=current_target_opacity,
            target_active_color_rgb=[
              current_target_active_color.red(),
              current_target_active_color.green(),
              current_target_active_color.blue(),
            ],
            target_active_opacity=current_target_active_opacity,
          )
          await context.log("BehavState=start_on")
      else:
        if fail_on_touch_input and touch_detected_this_trial:
          return await fail_for_touch_input(now)

        if joystick_is_active and not joystick_active_this_trial:
          ignored_idle_trial_count = 0
          first_movement_time = now
          append_event("first_joystick_movement", now, joystick_x=jx, joystick_y=jy)
        if joystick_is_active:
          joystick_active_this_trial = True
          if current_attempt is not None:
            current_attempt["joystick_active"] = True
        dist_to_target = math.hypot(cursor_px_x - target_px_x, cursor_px_y - target_px_y)
        cursor_inside_target = (dist_to_target <= target_radius_px)
        if cursor_inside_target and not previous_cursor_inside_target:
          target_entry_count += 1
          if first_target_entry_time is None:
            first_target_entry_time = now
          append_event("target_entry", now, cursor_x=cursor_x, cursor_y=cursor_y, entry_count=target_entry_count)
        if cursor_inside_target:
          if hold_start is None:
            hold_start = now
            if first_hold_start_time is None:
              first_hold_start_time = now
            append_event("hold_start", now, cursor_x=cursor_x, cursor_y=cursor_y, entry_count=target_entry_count)
            hold_progress_ratio = 0.0
          elif now - hold_start >= current_hold_time:
            hold_progress_ratio = 1.0
            append_event("hold_complete", now, hold_duration_s=now - hold_start)
            await deliver_reward_repeats(current_reward_channel, 1)
            append_event("reward_triggered", now, reward_count=1, reward_channel=current_reward_channel, reward_scale=clamp_float(task_config.get("reward_scale", 1.0), 0.0, 100.0, 1.0))
            streak_count += 1
            task_config["_streak_count"] = streak_count
            bonus_hit = (
              task_animation_enabled
              and streak_bonus_threshold > 0
              and streak_count % streak_bonus_threshold == 0
            )
            if bonus_hit:
              await deliver_reward_repeats(current_reward_channel, streak_bonus_reward_count)
              append_event("bonus_reward_triggered", now, reward_count=streak_bonus_reward_count, reward_channel=current_reward_channel, reward_scale=clamp_float(task_config.get("reward_scale", 1.0), 0.0, 100.0, 1.0))
              if streak_reset_on_bonus:
                streak_count = 0
                task_config["_streak_count"] = 0
            success_visual_duration_s = 0.0
            if target_animation_enabled and (
              (show_success_pop and success_pop_duration_s > 0.0)
              or show_success_particles
            ):
              success_pop_x = target_x
              success_pop_y = target_y
              visual_start_time = time.perf_counter()
              if show_success_pop and success_pop_duration_s > 0.0:
                success_pop_start = visual_start_time
                success_visual_duration_s = max(success_visual_duration_s, success_pop_duration_s)
              if show_success_particles:
                spawn_success_particles(visual_start_time)
                if success_particles:
                  success_visual_duration_s = max(
                    success_visual_duration_s,
                    max(float(particle.get("duration", 0.26)) for particle in success_particles),
                  )
              visual_end_time = visual_start_time + success_visual_duration_s
              while time.perf_counter() < visual_end_time:
                context.widget.update()
                await context.sleep(datetime.timedelta(seconds=0.01))
            append_event("success", now, streak_count=streak_count)
            finalize_attempt("success", now)
            success_sound.play()
            await context.log("BehavState=success")
            return TaskResult(success=True)
          else:
            hold_progress_ratio = max(0.0, min(1.0, (now - hold_start) / max(0.001, current_hold_time)))
        else:
          if previous_cursor_inside_target:
            append_event("target_exit", now, cursor_x=cursor_x, cursor_y=cursor_y, entry_count=target_entry_count)
          if hold_start is not None:
            append_event("hold_break", now, hold_duration_s=now - hold_start)
          hold_start = None
          hold_progress_ratio = 0.0
        previous_cursor_inside_target = cursor_inside_target

        if now - trial_start >= trial_timeout:
          if ignore_idle_trial_failures and not joystick_active_this_trial:
            ignored_idle_trial_count += 1
            hold_start = None
            state = "intertrial"
            reset_intertrial_gate(now)
            state_brightness = 0
            append_event("ignored_idle_timeout", now, ignored_idle_trial_count=ignored_idle_trial_count)
            finalize_attempt("ignored_idle", now, failure_reason="timeout_without_movement")
            if ignored_idle_trial_count > ignored_idle_sample_clear_threshold:
              clear_joystick_samples_for_prolonged_idle(now)
            await context.log("BehavState=intertrial")
            continue
          streak_count = 0
          task_config["_streak_count"] = 0
          append_event(
            "fail",
            now,
            failure_reason="timeout_without_movement" if not joystick_active_this_trial else "timeout_after_movement",
          )
          finalize_attempt(
            "fail",
            now,
            failure_reason="timeout_without_movement" if not joystick_active_this_trial else "timeout_after_movement",
          )
          fail_sound.play()
          await context.log("BehavState=fail")
          return TaskResult(success=False)

      context.widget.update()
      await context.sleep(datetime.timedelta(seconds=0.01))
  finally:
    task_config["_last_cursor_x"] = float(cursor_x)
    task_config["_last_cursor_y"] = float(cursor_y)
    persist_operator_key_state()
    analog_task.cancel()
    try:
      await analog_task
    except asyncio.CancelledError:
      pass
    except Exception:
      pass
