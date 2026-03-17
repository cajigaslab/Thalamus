"""
Joystick introduction task.

Goal:
- Teach joystick-to-cursor agency with a simple visual cursor and target.
- Cursor is drawn inside the task canvas only (does not control system mouse).

Two control modes:
- cumulative: joystick acts like velocity input (mouse-like accumulation).
- direct: joystick directly maps to cursor position around center.
"""

import asyncio
import datetime
import logging
import math
import random
import time
import typing

from ..qt import *
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from .widgets import Form
from .util import create_task_with_exc_handling, CanvasPainterProtocol, TaskContextProtocol, TaskResult
from ..config import *

LOGGER = logging.getLogger(__name__)

DEFAULT_TARGET_RADIUS_RATIO = 0.08
DEFAULT_TARGET_COLOR = [0, 220, 60]
DEFAULT_TARGET_HOLD_TIME = 0.40

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
  if "cursor_diameter_ratio" not in task_config:
    task_config["cursor_diameter_ratio"] = 0.1
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
  if "ignore_idle_trial_failures" not in task_config:
    if "pause_timeout_while_idle" in task_config:
      task_config["ignore_idle_trial_failures"] = bool(task_config["pause_timeout_while_idle"])
    else:
      task_config["ignore_idle_trial_failures"] = False
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
  if "success_pop_duration_s" not in task_config:
    task_config["success_pop_duration_s"] = 0.12

  form = Form.build(
    task_config, ["Parameter", "Value"],
    Form.String("Joystick Node", "joystick_node", "Joystick"),
    Form.Choice("Control Mode", "control_mode", [
      ("Cumulative", "cumulative"),
      ("Direct", "direct"),
    ]),
    Form.Bool("Cursor-Only Free Play", "cursor_only_mode", False),
    Form.Choice("Free Play End Key", "free_play_end_key", [
      ("Space", "space"),
      ("Q", "q"),
      ("Enter", "enter"),
    ]),
    Form.Constant("Cumulative Speed", "cumulative_speed", 0.70, precision=3),
    Form.Bool("Zero-Drift Mode", "zero_drift_mode", True),
    Form.Constant("Zero-Drift Buffer", "zero_drift_buffer", 0.05, precision=3),
    Form.Constant("Direct Range", "direct_range", 0.45, precision=3),
    Form.Constant("Cursor Diameter Ratio", "cursor_diameter_ratio", 0.1, precision=3),
    Form.Color("Cursor Color", "cursor_color", QColor(255, 70, 70)),
    Form.Constant("Task Region Center X", "task_region_x", 0.5, precision=3),
    Form.Constant("Task Region Center Y", "task_region_y", 0.270, precision=3),
    Form.Constant("Task Region Width", "task_region_width", 0.5, precision=3),
    Form.Constant("Task Region Height", "task_region_height", 0.67, precision=3),
    Form.Constant("Reward Channel", "reward_channel", 0, precision=0),
    Form.Constant("Trial Timeout (s)", "trial_timeout", 0.5, "s", precision=3),
    Form.Bool("Ignore Idle Trial Failures", "ignore_idle_trial_failures", False),
    Form.Constant("Intertrial Interval (s)", "intertrial_interval", 1.0, "s", precision=3),
  )
  layout.addWidget(form)

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
        "target_color": DEFAULT_TARGET_COLOR.copy()
      }
    ]

  targets = task_config["targets"]
  target_table = QTableWidget()
  target_table.setColumnCount(7)
  target_table.setHorizontalHeaderLabels(["Enabled", "Name", "X", "Y", "Radius", "Hold (s)", "Color"])
  target_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
  target_table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
  target_table.horizontalHeader().setStretchLastSection(True)
  target_table.verticalHeader().setVisible(False)
  target_table.setMinimumHeight(190)

  def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))

  def normalize_target(target: typing.Any) -> typing.Dict[str, typing.Any]:
    t = dict(target) if isinstance(target, dict) else {}
    raw_color = t.get("target_color", DEFAULT_TARGET_COLOR)
    if isinstance(raw_color, (list, tuple)) and len(raw_color) >= 3:
      color = [
        max(0, min(255, int(raw_color[0]))),
        max(0, min(255, int(raw_color[1]))),
        max(0, min(255, int(raw_color[2]))),
      ]
    else:
      color = [0, 220, 60]
    return {
      "name": str(t.get("name", "")),
      "enabled": bool(t.get("enabled", True)),
      "x_norm": clamp(float(t.get("x_norm", 0.75)), 0.0, 1.0),
      "y_norm": clamp(float(t.get("y_norm", 0.50)), 0.0, 1.0),
      "radius_ratio": clamp(float(t.get("radius_ratio", DEFAULT_TARGET_RADIUS_RATIO)), 0.01, 0.5),
      "hold_time": clamp(float(t.get("hold_time", DEFAULT_TARGET_HOLD_TIME)), 0.01, 10.0),
      "target_color": color,
    }

  def make_default_target(index: int) -> typing.Dict[str, typing.Any]:
    return {
      "name": f"Target {index}",
      "enabled": True,
      "x_norm": 0.75,
      "y_norm": 0.50,
      "radius_ratio": DEFAULT_TARGET_RADIUS_RATIO,
      "hold_time": DEFAULT_TARGET_HOLD_TIME,
      "target_color": DEFAULT_TARGET_COLOR.copy(),
    }

  class LayoutPreview(QWidget):
    def __init__(
      self,
      targets_ref: typing.List[typing.Dict[str, typing.Any]],
      cursor_radius_getter: typing.Callable[[], float],
      select_callback: typing.Callable[[int], None],
      update_callback: typing.Callable[[], None],
      parent: typing.Optional[QWidget] = None,
    ) -> None:
      super().__init__(parent)
      self.targets_ref = targets_ref
      self.cursor_radius_getter = cursor_radius_getter
      self.select_callback = select_callback
      self.update_callback = update_callback
      self.selected_index = -1
      self.drag_index = -1
      self.setMinimumSize(420, 420)
      self.setMouseTracking(True)

    def set_selected_index(self, index: int) -> None:
      self.selected_index = index
      self.update()

    def _region_rect(self) -> QRectF:
      w = float(max(1, self.width()))
      h = float(max(1, self.height()))
      margin = 18.0
      avail_w = max(1.0, w - 2.0 * margin)
      avail_h = max(1.0, h - 2.0 * margin)
      cfg_w = max(0.05, min(1.0, float(task_config.get("task_region_width", 0.5))))
      cfg_h = max(0.05, min(1.0, float(task_config.get("task_region_height", 0.67))))
      scale = min(avail_w / cfg_w, avail_h / cfg_h)
      region_w = cfg_w * scale
      region_h = cfg_h * scale
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
      self.drag_index = index
      self.selected_index = index
      self.select_callback(index)
      self.update()

    def mouseMoveEvent(self, event: typing.Any) -> None:
      if self.drag_index >= 0:
        self._move_target_to_pos(self.drag_index, self._event_pos(event))
        return
      super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event: typing.Any) -> None:
      if event.button() == Qt.MouseButton.LeftButton:
        self.drag_index = -1
      super().mouseReleaseEvent(event)

    def paintEvent(self, event: typing.Any) -> None:
      painter = QPainter(self)
      painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
      full_rect = self.rect()
      painter.fillRect(full_rect, QColor(24, 24, 24))
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

      for i, target in enumerate(self.targets_ref):
        center = self._target_center(region_rect, target)
        radius = self._target_radius_px(region_rect, target)
        rgb = target.get("target_color", DEFAULT_TARGET_COLOR)
        color = QColor(int(rgb[0]), int(rgb[1]), int(rgb[2]))
        if not bool(target.get("enabled", True)):
          color.setAlpha(80)
        painter.setPen(QPen(QColor(255, 255, 255) if i == self.selected_index else color, 2))
        painter.setBrush(color)
        painter.drawEllipse(center, radius, radius)
        if i == self.selected_index:
          painter.setPen(QPen(QColor(255, 255, 255), 2, Qt.PenStyle.DashLine))
          painter.setBrush(Qt.BrushStyle.NoBrush)
          painter.drawEllipse(center, radius + 6.0, radius + 6.0)
        painter.setPen(QPen(QColor(255, 255, 255), 1))
        label = str(target.get("name", f"T{i + 1}"))
        painter.drawText(int(center.x() + radius + 6.0), int(center.y() - radius - 4.0), label)

      cursor_center = QPointF(region_rect.center().x(), region_rect.center().y())
      cursor_radius = max(0.005, min(0.5, float(self.cursor_radius_getter()))) * min(region_rect.width(), region_rect.height())
      cursor_color_rgb = task_config.get("cursor_color", [255, 70, 70])
      cursor_color = QColor(int(cursor_color_rgb[0]), int(cursor_color_rgb[1]), int(cursor_color_rgb[2]))
      painter.setPen(QPen(QColor(255, 255, 255), 1))
      painter.setBrush(cursor_color)
      painter.drawEllipse(cursor_center, cursor_radius, cursor_radius)

      painter.setPen(QPen(QColor(200, 200, 200), 1))
      painter.drawText(12, 18, "Drag target centers to reposition them inside the task region.")

  def open_layout_editor() -> None:
    dialog = QDialog(result, Qt.WindowType.Window)
    dialog.setWindowTitle("Target Layout Editor")
    dialog.setModal(True)
    dialog.resize(900, 560)

    draft_targets = [normalize_target(target) for target in list(targets)]
    if not draft_targets:
      draft_targets.append(make_default_target(1))

    dialog_layout = QHBoxLayout(dialog)
    preview: typing.Optional[LayoutPreview] = None
    selected_index = 0
    draft_cursor_radius = max(0.005, min(0.5, float(task_config.get("cursor_diameter_ratio", 0.1)) / 2.0))

    side_panel = QWidget(dialog)
    side_layout = QVBoxLayout(side_panel)
    side_layout.setContentsMargins(0, 0, 0, 0)

    target_list = QListWidget()
    target_list.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
    side_layout.addWidget(QLabel("Targets"))
    side_layout.addWidget(target_list, 1)

    form_panel = QGroupBox("Selected Target")
    form_layout = QFormLayout(form_panel)
    name_edit = QLineEdit()
    enabled_box = QCheckBox("Enabled")
    x_spin = QDoubleSpinBox()
    y_spin = QDoubleSpinBox()
    radius_spin = QDoubleSpinBox()
    hold_spin = QDoubleSpinBox()
    color_button = QPushButton("Choose Color")

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

    form_layout.addRow("Name", name_edit)
    form_layout.addRow("", enabled_box)
    form_layout.addRow("X", x_spin)
    form_layout.addRow("Y", y_spin)
    form_layout.addRow("Radius", radius_spin)
    form_layout.addRow("Hold (s)", hold_spin)
    form_layout.addRow("Color", color_button)
    side_layout.addWidget(form_panel)

    cursor_panel = QGroupBox("Cursor Reference")
    cursor_layout = QFormLayout(cursor_panel)
    cursor_radius_spin = QDoubleSpinBox()
    cursor_radius_spin.setRange(0.005, 0.5)
    cursor_radius_spin.setDecimals(3)
    cursor_radius_spin.setSingleStep(0.005)
    cursor_radius_spin.setValue(draft_cursor_radius)
    cursor_layout.addRow("Cursor Radius", cursor_radius_spin)
    side_layout.addWidget(cursor_panel)

    button_row = QWidget()
    button_row_layout = QHBoxLayout(button_row)
    button_row_layout.setContentsMargins(0, 0, 0, 0)
    add_button = QPushButton("Add")
    remove_button = QPushButton("Remove")
    save_button = QPushButton("Save")
    cancel_button = QPushButton("Cancel")
    button_row_layout.addWidget(add_button)
    button_row_layout.addWidget(remove_button)
    button_row_layout.addStretch(1)
    button_row_layout.addWidget(save_button)
    button_row_layout.addWidget(cancel_button)
    side_layout.addWidget(button_row)

    def refresh_target_list() -> None:
      target_list.blockSignals(True)
      target_list.clear()
      for i, target in enumerate(draft_targets):
        label = str(target.get("name", "")).strip() or f"Target {i + 1}"
        if not bool(target.get("enabled", True)):
          label += " (disabled)"
        target_list.addItem(label)
      target_list.blockSignals(False)

    def update_color_button() -> None:
      if 0 <= selected_index < len(draft_targets):
        rgb = draft_targets[selected_index].get("target_color", DEFAULT_TARGET_COLOR)
      else:
        rgb = DEFAULT_TARGET_COLOR
      color_button.setStyleSheet(
        f"background-color: rgb({int(rgb[0])}, {int(rgb[1])}, {int(rgb[2])});"
      )

    def populate_controls() -> None:
      controls_enabled = 0 <= selected_index < len(draft_targets)
      for widget in (name_edit, enabled_box, x_spin, y_spin, radius_spin, hold_spin, color_button, remove_button):
        widget.setEnabled(controls_enabled)
      if not controls_enabled:
        target_list.blockSignals(True)
        name_edit.blockSignals(True)
        enabled_box.blockSignals(True)
        x_spin.blockSignals(True)
        y_spin.blockSignals(True)
        radius_spin.blockSignals(True)
        hold_spin.blockSignals(True)
        name_edit.setText("")
        enabled_box.setChecked(False)
        x_spin.setValue(0.0)
        y_spin.setValue(0.0)
        radius_spin.setValue(DEFAULT_TARGET_RADIUS_RATIO)
        hold_spin.setValue(DEFAULT_TARGET_HOLD_TIME)
        hold_spin.blockSignals(False)
        radius_spin.blockSignals(False)
        y_spin.blockSignals(False)
        x_spin.blockSignals(False)
        enabled_box.blockSignals(False)
        name_edit.blockSignals(False)
        target_list.blockSignals(False)
        update_color_button()
        if preview is not None:
          preview.set_selected_index(-1)
        return

      target = draft_targets[selected_index]
      target_list.blockSignals(True)
      name_edit.blockSignals(True)
      enabled_box.blockSignals(True)
      x_spin.blockSignals(True)
      y_spin.blockSignals(True)
      radius_spin.blockSignals(True)
      hold_spin.blockSignals(True)
      name_edit.setText(str(target.get("name", "")))
      enabled_box.setChecked(bool(target.get("enabled", True)))
      x_spin.setValue(float(target.get("x_norm", 0.75)))
      y_spin.setValue(float(target.get("y_norm", 0.50)))
      radius_spin.setValue(float(target.get("radius_ratio", DEFAULT_TARGET_RADIUS_RATIO)))
      hold_spin.setValue(float(target.get("hold_time", DEFAULT_TARGET_HOLD_TIME)))
      hold_spin.blockSignals(False)
      radius_spin.blockSignals(False)
      y_spin.blockSignals(False)
      x_spin.blockSignals(False)
      enabled_box.blockSignals(False)
      name_edit.blockSignals(False)
      update_color_button()
      target_list.setCurrentRow(selected_index)
      target_list.blockSignals(False)
      if preview is not None:
        preview.set_selected_index(selected_index)

    def select_index(index: int) -> None:
      nonlocal selected_index
      if index < 0 or index >= len(draft_targets):
        selected_index = -1
      else:
        selected_index = index
      populate_controls()

    def refresh_editor() -> None:
      refresh_target_list()
      populate_controls()
      if preview is not None:
        preview.update()

    preview = LayoutPreview(draft_targets, lambda: draft_cursor_radius, select_index, refresh_editor, dialog)
    dialog_layout.addWidget(preview, 1)
    dialog_layout.addWidget(side_panel)

    def apply_field_changes() -> None:
      if not (0 <= selected_index < len(draft_targets)):
        return
      draft_targets[selected_index] = normalize_target({
        **draft_targets[selected_index],
        "name": name_edit.text(),
        "enabled": enabled_box.isChecked(),
        "x_norm": x_spin.value(),
        "y_norm": y_spin.value(),
        "radius_ratio": radius_spin.value(),
        "hold_time": hold_spin.value(),
      })
      refresh_editor()

    def choose_color() -> None:
      if not (0 <= selected_index < len(draft_targets)):
        return
      current_rgb = draft_targets[selected_index].get("target_color", DEFAULT_TARGET_COLOR)
      selected = QColorDialog.getColor(QColor(*current_rgb), dialog, "Select Target Color")
      if not selected.isValid():
        return
      draft_targets[selected_index]["target_color"] = [selected.red(), selected.green(), selected.blue()]
      refresh_editor()

    name_edit.textEdited.connect(lambda _text: apply_field_changes())
    enabled_box.toggled.connect(lambda _checked: apply_field_changes())
    x_spin.valueChanged.connect(lambda _value: apply_field_changes())
    y_spin.valueChanged.connect(lambda _value: apply_field_changes())
    radius_spin.valueChanged.connect(lambda _value: apply_field_changes())
    hold_spin.valueChanged.connect(lambda _value: apply_field_changes())
    color_button.clicked.connect(choose_color)
    target_list.currentRowChanged.connect(select_index)

    def on_cursor_radius_changed(value: float) -> None:
      nonlocal draft_cursor_radius
      draft_cursor_radius = max(0.005, min(0.5, float(value)))
      if preview is not None:
        preview.update()

    cursor_radius_spin.valueChanged.connect(on_cursor_radius_changed)

    def add_layout_target() -> None:
      nonlocal selected_index
      if 0 <= selected_index < len(draft_targets):
        new_target = normalize_target(draft_targets[selected_index])
        new_target["name"] = f"{new_target.get('name', '').strip() or 'Target'} Copy"
      else:
        new_target = make_default_target(len(draft_targets) + 1)
      draft_targets.append(new_target)
      selected_index = len(draft_targets) - 1
      refresh_editor()

    def remove_layout_target() -> None:
      nonlocal selected_index
      if not (0 <= selected_index < len(draft_targets)):
        return
      del draft_targets[selected_index]
      if not draft_targets:
        draft_targets.append(make_default_target(1))
      selected_index = min(selected_index, len(draft_targets) - 1)
      refresh_editor()

    def save_layout() -> None:
      normalized_targets = [normalize_target(target) for target in draft_targets]
      while targets:
        del targets[len(targets) - 1]
      for target in normalized_targets:
        targets.append(target)
      task_config["cursor_diameter_ratio"] = max(0.01, min(1.0, draft_cursor_radius * 2.0))
      sync_table_from_config()
      if 0 <= selected_index < target_table.rowCount():
        target_table.selectRow(selected_index)
      dialog.accept()

    add_button.clicked.connect(add_layout_target)
    remove_button.clicked.connect(remove_layout_target)
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
    dialog.exec()

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
    color_button = QPushButton()
    rgb = target["target_color"]
    color_button.setStyleSheet(f"background-color: rgb({rgb[0]}, {rgb[1]}, {rgb[2]});")
    color_button.setText("")

    def on_pick_color() -> None:
      if row < 0 or row >= len(targets):
        return
      current_rgb = targets[row].get("target_color", DEFAULT_TARGET_COLOR)
      selected = QColorDialog.getColor(QColor(*current_rgb), result, "Select Target Color")
      if selected.isValid():
        new_rgb = [selected.red(), selected.green(), selected.blue()]
        targets[row]["target_color"] = new_rgb
        color_button.setStyleSheet(f"background-color: rgb({new_rgb[0]}, {new_rgb[1]}, {new_rgb[2]});")

    color_button.clicked.connect(on_pick_color)
    target_table.setCellWidget(row, 6, color_button)

  def sync_table_from_config() -> None:
    target_table.blockSignals(True)
    target_table.setRowCount(0)
    for i, raw_target in enumerate(list(targets)):
      target = normalize_target(raw_target)
      targets[i] = target
      target_table.insertRow(i)
      write_table_row(i, target)
    target_table.blockSignals(False)

  def sync_row_to_config(row: int) -> None:
    if row < 0 or row >= len(targets):
      return
    enabled_item = target_table.item(row, 0)
    name_item = target_table.item(row, 1)
    x_item = target_table.item(row, 2)
    y_item = target_table.item(row, 3)
    radius_item = target_table.item(row, 4)
    hold_item = target_table.item(row, 5)
    if not all((enabled_item, name_item, x_item, y_item, radius_item, hold_item)):
      return
    try:
      x_val = clamp(float(x_item.text()), 0.0, 1.0)
      y_val = clamp(float(y_item.text()), 0.0, 1.0)
      radius_val = clamp(float(radius_item.text()), 0.01, 0.5)
      hold_val = clamp(float(hold_item.text()), 0.01, 10.0)
    except ValueError:
      target_table.blockSignals(True)
      write_table_row(row, normalize_target(targets[row]))
      target_table.blockSignals(False)
      return
    targets[row] = {
      "name": str(name_item.text()),
      "enabled": enabled_item.checkState() == Qt.CheckState.Checked,
      "x_norm": x_val,
      "y_norm": y_val,
      "radius_ratio": radius_val,
      "hold_time": hold_val,
      "target_color": normalize_target(targets[row]).get("target_color", DEFAULT_TARGET_COLOR.copy()),
    }
    target_table.blockSignals(True)
    write_table_row(row, targets[row])
    target_table.blockSignals(False)

  def on_item_changed(item: QTableWidgetItem) -> None:
    if item.column() == 6:
      return
    sync_row_to_config(item.row())

  target_table.itemChanged.connect(on_item_changed)

  controls = QWidget()
  controls_layout = QHBoxLayout(controls)
  controls_layout.setContentsMargins(0, 0, 0, 0)
  add_target_button = QPushButton("Add Target")
  remove_target_button = QPushButton("Remove Selected")
  edit_layout_button = QPushButton("Edit Layout...")
  bulk_field_combo = QComboBox()
  bulk_field_combo.addItems([
    "Enabled",
    "Name",
    "X",
    "Y",
    "Radius",
    "Hold (s)",
    "Color",
  ])
  bulk_field_combo.setCurrentText("Radius")
  apply_to_all_button = QPushButton("Apply Field to All")
  controls_layout.addWidget(add_target_button)
  controls_layout.addWidget(remove_target_button)
  controls_layout.addWidget(edit_layout_button)
  controls_layout.addWidget(QLabel("Field:"))
  controls_layout.addWidget(bulk_field_combo)
  controls_layout.addWidget(apply_to_all_button)
  controls_layout.addStretch(1)

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
    if not targets:
      targets.append({
        **make_default_target(1)
      })
    sync_table_from_config()

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
    elif field == "Color":
      rgb = list(source["target_color"])
      for i in range(len(targets)):
        targets[i] = normalize_target(targets[i])
        targets[i]["target_color"] = rgb.copy()

    sync_table_from_config()
    if target_table.rowCount() > 0:
      target_table.selectRow(src_row)

  add_target_button.clicked.connect(add_target)
  remove_target_button.clicked.connect(remove_target)
  edit_layout_button.clicked.connect(open_layout_editor)
  apply_to_all_button.clicked.connect(apply_selected_field_to_all)

  layout.addWidget(QLabel("Targets (rows = targets):"))
  layout.addWidget(target_table)
  layout.addWidget(controls)

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

  animation_layout.addWidget(QLabel("Inside Tint Strength (%)"), 7, 0)
  inside_tint_spin = QSpinBox()
  inside_tint_spin.setRange(0, 100)
  inside_tint_spin.setValue(int(max(0, min(100, int(task_config.get("inside_tint_strength_pct", 20))))))
  inside_tint_spin.valueChanged.connect(lambda v: task_config.update({"inside_tint_strength_pct": int(v)}))
  animation_layout.addWidget(inside_tint_spin, 7, 1)

  add_anim_checkbox(8, "Show Hold Progress Ring", "show_hold_progress_ring")
  add_anim_checkbox(9, "Show Success Pop", "show_success_pop")

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

  task_config = context.config["queue"][0]
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
  cursor_diameter_ratio = float(task_config.get("cursor_diameter_ratio", 0.1))
  cursor_color = QColor(*task_config.get("cursor_color", [255, 70, 70]))
  task_region_x = float(task_config.get("task_region_x", 0.5))
  task_region_y = float(task_config.get("task_region_y", 0.270))
  task_region_width = float(task_config.get("task_region_width", 0.5))
  task_region_height = float(task_config.get("task_region_height", 0.67))
  reward_channel = int(task_config.get("reward_channel", 0))
  target_radius_ratio = DEFAULT_TARGET_RADIUS_RATIO
  target_color = QColor(*DEFAULT_TARGET_COLOR)
  hold_time = DEFAULT_TARGET_HOLD_TIME
  trial_timeout = float(task_config.get("trial_timeout", 0.5))
  ignore_idle_trial_failures = bool(task_config.get("ignore_idle_trial_failures", False))
  intertrial_interval = float(task_config.get("intertrial_interval", 1.0))
  configured_targets = task_config.get("targets", [])
  animations_enabled = bool(task_config.get("animations_enabled", False))
  task_animation_enabled = animations_enabled and bool(task_config.get("task_animation_enabled", True))
  target_animation_enabled = animations_enabled and bool(task_config.get("target_animation_enabled", True))
  show_streak_hud = bool(task_config.get("show_streak_hud", True))
  streak_bonus_threshold = max(0, int(task_config.get("streak_bonus_threshold", 0)))
  streak_bonus_reward_count = max(1, int(task_config.get("streak_bonus_reward_count", 1)))
  streak_reset_on_bonus = bool(task_config.get("streak_reset_on_bonus", False))
  inside_tint_strength = max(0.0, min(1.0, float(task_config.get("inside_tint_strength_pct", 20)) / 100.0))
  show_hold_progress_ring = bool(task_config.get("show_hold_progress_ring", True))
  show_success_pop = bool(task_config.get("show_success_pop", True))
  success_pop_duration_s = max(0.0, min(1.0, float(task_config.get("success_pop_duration_s", 0.12))))
  streak_count = max(0, int(task_config.get("_streak_count", 0)))

  cursor_x = 0.5
  cursor_y = 0.5
  target_x = 0.5
  target_y = 0.5

  hold_start: typing.Optional[float] = None
  trial_start = time.perf_counter()
  last_tick = trial_start
  state = "iti"
  iti_end = trial_start + intertrial_interval
  current_target_radius_ratio = target_radius_ratio
  current_hold_time = hold_time
  current_target_color = target_color
  free_play_end_requested = False
  joystick_x = 0.0
  joystick_y = 0.0
  joystick_active_this_trial = False
  cursor_inside_target = False
  hold_progress_ratio = 0.0
  success_pop_start: typing.Optional[float] = None
  success_pop_x = 0.5
  success_pop_y = 0.5

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
    key = event.key()
    if free_play_end_key == "q":
      free_play_end_requested = (key == Qt.Key.Key_Q)
    elif free_play_end_key == "enter":
      free_play_end_requested = (key == Qt.Key.Key_Return or key == Qt.Key.Key_Enter)
    else:
      free_play_end_requested = (key == Qt.Key.Key_Space)

  async def analog_processor(stream: typing.Any) -> None:
    nonlocal joystick_x, joystick_y
    try:
      async for message in stream:
        if len(message.data) >= 2:
          joystick_x = float(message.data[0])
          joystick_y = float(message.data[1])
    finally:
      stream.cancel()

  async def deliver_reward() -> None:
    on_time_ms = int(context.get_reward(reward_channel))
    if on_time_ms <= 0:
      LOGGER.info("Reward skipped: channel=%d returned %d ms", reward_channel, on_time_ms)
      return
    signal = thalamus_pb2.AnalogResponse(
      data=[5, 0],
      spans=[thalamus_pb2.Span(begin=0, end=2, name='Reward')],
      sample_intervals=[1_000_000 * on_time_ms],
    )
    LOGGER.info("Delivering reward channel=%d duration_ms=%d", reward_channel, on_time_ms)
    await context.inject_analog('Reward', signal)

  async def deliver_reward_repeats(repeats: int) -> None:
    for i in range(max(0, repeats)):
      await deliver_reward()
      if i < repeats - 1:
        await context.sleep(datetime.timedelta(seconds=0.05))

  def place_target() -> typing.Tuple[float, float, float, float, QColor]:
    enabled_targets = []
    for target in configured_targets:
      if not isinstance(target, dict):
        continue
      if not bool(target.get("enabled", True)):
        continue
      tx = max(0.0, min(1.0, float(target.get("x_norm", 0.75))))
      ty = max(0.0, min(1.0, float(target.get("y_norm", 0.50))))
      tr = max(0.01, min(0.5, float(target.get("radius_ratio", target_radius_ratio))))
      th = max(0.01, min(10.0, float(target.get("hold_time", hold_time))))
      raw_color = target.get("target_color", [target_color.red(), target_color.green(), target_color.blue()])
      if isinstance(raw_color, (list, tuple)) and len(raw_color) >= 3:
        tc = QColor(
          max(0, min(255, int(raw_color[0]))),
          max(0, min(255, int(raw_color[1]))),
          max(0, min(255, int(raw_color[2]))),
        )
      else:
        tc = target_color
      enabled_targets.append((tx, ty, tr, th, tc))
    if enabled_targets:
      return random.choice(enabled_targets)
    return 0.75, 0.50, target_radius_ratio, hold_time, target_color

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

    if (not cursor_only_mode) and state == "active":
      draw_target_color = QColor(current_target_color)
      if target_animation_enabled and cursor_inside_target and inside_tint_strength > 0.0:
        r = int(draw_target_color.red() + (255 - draw_target_color.red()) * inside_tint_strength)
        g = int(draw_target_color.green() + (255 - draw_target_color.green()) * inside_tint_strength)
        b = int(draw_target_color.blue() + (255 - draw_target_color.blue()) * inside_tint_strength)
        draw_target_color = QColor(r, g, b)
      painter.setPen(QPen(draw_target_color, 1))
      painter.setBrush(draw_target_color)
      painter.drawEllipse(tx - target_radius_px, ty - target_radius_px, 2 * target_radius_px, 2 * target_radius_px)
      if target_animation_enabled and show_hold_progress_ring and hold_progress_ratio > 0.0:
        ring_radius = target_radius_px + max(3, int(0.015 * min_dim))
        ring_width = max(2, int(0.008 * min_dim))
        painter.setPen(QPen(QColor(255, 255, 255, 220), ring_width))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        diameter = 2 * ring_radius
        painter.drawArc(
          tx - ring_radius,
          ty - ring_radius,
          diameter,
          diameter,
          90 * 16,
          -int(360 * 16 * max(0.0, min(1.0, hold_progress_ratio))),
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

  context.widget.renderer = renderer
  context.widget.key_release_handler = on_key_release

  channel = context.get_channel('localhost:50050')
  stub = thalamus_pb2_grpc.ThalamusStub(channel)
  request = thalamus_pb2.AnalogRequest(
    node=thalamus_pb2.NodeSelector(name=joystick_node),
    channel_names=['X', 'Y'],
  )
  stream = stub.analog(request)
  analog_task = create_task_with_exc_handling(analog_processor(stream))

  try:
    if cursor_only_mode:
      await context.log("BehavState=free_play")
    else:
      await context.log(f"BehavState={state}")
    while True:
      now = time.perf_counter()
      dt = max(0.0, min(0.05, now - last_tick))
      last_tick = now

      if control_mode == "direct":
        jx, jy = apply_direction_influence(joystick_x, joystick_y)
        cursor_x = 0.5 + jx * direct_range
        cursor_y = 0.5 + jy * direct_range
      else:
        jx, jy = apply_direction_influence(joystick_x, joystick_y)
        if zero_drift_mode:
          # Radial deadband to suppress small spring-back offsets near center.
          if math.hypot(jx, jy) < zero_drift_buffer:
            jx = 0.0
            jy = 0.0
        cursor_x += jx * cumulative_speed * dt
        cursor_y += jy * cumulative_speed * dt

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
      cursor_inside_target = False
      hold_progress_ratio = 0.0

      if cursor_only_mode:
        if free_play_end_requested:
          await context.log("BehavState=success")
          return TaskResult(success=True)
      elif state == "iti":
        if now >= iti_end:
          target_x, target_y, current_target_radius_ratio, current_hold_time, current_target_color = place_target()
          hold_start = None
          trial_start = now
          joystick_active_this_trial = False
          state = "active"
          await context.log("BehavState=active")
      else:
        if joystick_is_active:
          joystick_active_this_trial = True
        dist_to_target = math.hypot(cursor_px_x - target_px_x, cursor_px_y - target_px_y)
        cursor_inside_target = (dist_to_target <= target_radius_px)
        if cursor_inside_target:
          if hold_start is None:
            hold_start = now
            hold_progress_ratio = 0.0
          elif now - hold_start >= current_hold_time:
            hold_progress_ratio = 1.0
            await deliver_reward_repeats(1)
            streak_count += 1
            task_config["_streak_count"] = streak_count
            bonus_hit = (
              task_animation_enabled
              and streak_bonus_threshold > 0
              and streak_count % streak_bonus_threshold == 0
            )
            if bonus_hit:
              await deliver_reward_repeats(streak_bonus_reward_count)
              if streak_reset_on_bonus:
                streak_count = 0
                task_config["_streak_count"] = 0
            if target_animation_enabled and show_success_pop and success_pop_duration_s > 0.0:
              success_pop_x = target_x
              success_pop_y = target_y
              success_pop_start = time.perf_counter()
              pop_end_time = success_pop_start + success_pop_duration_s
              while time.perf_counter() < pop_end_time:
                context.widget.update()
                await context.sleep(datetime.timedelta(seconds=0.01))
            await context.log("BehavState=success")
            return TaskResult(success=True)
          else:
            hold_progress_ratio = max(0.0, min(1.0, (now - hold_start) / max(0.001, current_hold_time)))
        else:
          hold_start = None
          hold_progress_ratio = 0.0

        if now - trial_start >= trial_timeout:
          if ignore_idle_trial_failures and not joystick_active_this_trial:
            hold_start = None
            state = "iti"
            iti_end = now + intertrial_interval
            await context.log("BehavState=iti")
            continue
          streak_count = 0
          task_config["_streak_count"] = 0
          await context.log("BehavState=fail")
          return TaskResult(success=False)

      context.widget.update()
      await context.sleep(datetime.timedelta(seconds=0.01))
  finally:
    analog_task.cancel()
    try:
      await analog_task
    except asyncio.CancelledError:
      pass
    except Exception:
      pass
