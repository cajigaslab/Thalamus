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
        if isinstance(watched, (QSpinBox, QDoubleSpinBox, QComboBox)):
          event.ignore()
          return True
      return super().eventFilter(watched, event)

  result = QWidget()
  layout = QVBoxLayout(result)
  result.setLayout(layout)

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
    Form.Constant("Cursor Diameter Ratio", "cursor_diameter_ratio", 0.03, precision=3),
    Form.Color("Cursor Color", "cursor_color", QColor(255, 70, 70)),
    Form.Constant("Task Region Center X", "task_region_x", 0.5, precision=3),
    Form.Constant("Task Region Center Y", "task_region_y", 0.5, precision=3),
    Form.Constant("Task Region Width", "task_region_width", 1.0, precision=3),
    Form.Constant("Task Region Height", "task_region_height", 1.0, precision=3),
    Form.Constant("Reward Channel", "reward_channel", 0, precision=0),
    Form.Constant("Trial Timeout (s)", "trial_timeout", 10.0, "s", precision=3),
    Form.Constant("Intertrial Interval (s)", "intertrial_interval", 1.0, "s", precision=3),
  )
  layout.addWidget(form)

  if "disable_up" not in task_config:
    task_config["disable_up"] = False
  if "disable_down" not in task_config:
    task_config["disable_down"] = False
  if "disable_left" not in task_config:
    task_config["disable_left"] = False
  if "disable_right" not in task_config:
    task_config["disable_right"] = False

  direction_row = QWidget()
  direction_layout = QHBoxLayout(direction_row)
  direction_layout.setContentsMargins(0, 0, 0, 0)
  direction_layout.addWidget(QLabel("Disable Directions:"))

  disable_up_box = QCheckBox("Up")
  disable_down_box = QCheckBox("Down")
  disable_left_box = QCheckBox("Left")
  disable_right_box = QCheckBox("Right")

  disable_up_box.setChecked(bool(task_config["disable_up"]))
  disable_down_box.setChecked(bool(task_config["disable_down"]))
  disable_left_box.setChecked(bool(task_config["disable_left"]))
  disable_right_box.setChecked(bool(task_config["disable_right"]))

  disable_up_box.toggled.connect(lambda v: task_config.update({"disable_up": bool(v)}))
  disable_down_box.toggled.connect(lambda v: task_config.update({"disable_down": bool(v)}))
  disable_left_box.toggled.connect(lambda v: task_config.update({"disable_left": bool(v)}))
  disable_right_box.toggled.connect(lambda v: task_config.update({"disable_right": bool(v)}))

  direction_layout.addWidget(disable_up_box)
  direction_layout.addWidget(disable_down_box)
  direction_layout.addWidget(disable_left_box)
  direction_layout.addWidget(disable_right_box)
  direction_layout.addStretch(1)
  layout.addWidget(direction_row)

  if "targets" not in task_config or not task_config["targets"]:
    task_config["targets"] = [
      {
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
  target_table.setColumnCount(6)
  target_table.setHorizontalHeaderLabels(["Enabled", "X", "Y", "Radius", "Hold (s)", "Color"])
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
      "enabled": bool(t.get("enabled", True)),
      "x_norm": clamp(float(t.get("x_norm", 0.75)), 0.0, 1.0),
      "y_norm": clamp(float(t.get("y_norm", 0.50)), 0.0, 1.0),
      "radius_ratio": clamp(float(t.get("radius_ratio", DEFAULT_TARGET_RADIUS_RATIO)), 0.01, 0.5),
      "hold_time": clamp(float(t.get("hold_time", DEFAULT_TARGET_HOLD_TIME)), 0.01, 10.0),
      "target_color": color,
    }

  def write_table_row(row: int, target: typing.Dict[str, typing.Any]) -> None:
    enabled_item = QTableWidgetItem("")
    enabled_item.setFlags(
      Qt.ItemFlag.ItemIsSelectable
      | Qt.ItemFlag.ItemIsEnabled
      | Qt.ItemFlag.ItemIsUserCheckable
    )
    enabled_item.setCheckState(Qt.CheckState.Checked if target["enabled"] else Qt.CheckState.Unchecked)
    target_table.setItem(row, 0, enabled_item)
    target_table.setItem(row, 1, QTableWidgetItem(f"{target['x_norm']:.3f}"))
    target_table.setItem(row, 2, QTableWidgetItem(f"{target['y_norm']:.3f}"))
    target_table.setItem(row, 3, QTableWidgetItem(f"{target['radius_ratio']:.3f}"))
    target_table.setItem(row, 4, QTableWidgetItem(f"{target['hold_time']:.3f}"))
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
    target_table.setCellWidget(row, 5, color_button)

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
    x_item = target_table.item(row, 1)
    y_item = target_table.item(row, 2)
    radius_item = target_table.item(row, 3)
    hold_item = target_table.item(row, 4)
    if not all((enabled_item, x_item, y_item, radius_item, hold_item)):
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
    if item.column() == 5:
      return
    sync_row_to_config(item.row())

  target_table.itemChanged.connect(on_item_changed)

  controls = QWidget()
  controls_layout = QHBoxLayout(controls)
  controls_layout.setContentsMargins(0, 0, 0, 0)
  add_target_button = QPushButton("Add Target")
  remove_target_button = QPushButton("Remove Selected")
  controls_layout.addWidget(add_target_button)
  controls_layout.addWidget(remove_target_button)
  controls_layout.addStretch(1)

  def add_target() -> None:
    targets.append({
      "enabled": True,
      "x_norm": 0.50,
      "y_norm": 0.50,
      "radius_ratio": DEFAULT_TARGET_RADIUS_RATIO,
      "hold_time": DEFAULT_TARGET_HOLD_TIME,
      "target_color": DEFAULT_TARGET_COLOR.copy()
    })
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
        "enabled": True,
        "x_norm": 0.75,
        "y_norm": 0.50,
        "radius_ratio": DEFAULT_TARGET_RADIUS_RATIO,
        "hold_time": DEFAULT_TARGET_HOLD_TIME,
        "target_color": DEFAULT_TARGET_COLOR.copy()
      })
    sync_table_from_config()

  add_target_button.clicked.connect(add_target)
  remove_target_button.clicked.connect(remove_target)

  layout.addWidget(QLabel("Targets (rows = targets):"))
  layout.addWidget(target_table)
  layout.addWidget(controls)
  sync_table_from_config()

  no_wheel_filter = NoWheelChangeFilter(result)
  result._no_wheel_filter = no_wheel_filter # type: ignore[attr-defined]
  for spinbox in result.findChildren(QSpinBox):
    spinbox.installEventFilter(no_wheel_filter)
  for spinbox in result.findChildren(QDoubleSpinBox):
    spinbox.installEventFilter(no_wheel_filter)
  for combo in result.findChildren(QComboBox):
    combo.installEventFilter(no_wheel_filter)

  return result


async def run(context: TaskContextProtocol) -> TaskResult:
  assert context.widget, "Widget is None; cannot render."

  task_config = context.config["queue"][0]
  joystick_node = str(task_config.get("joystick_node", "Joystick"))
  disable_up = bool(task_config.get("disable_up", False))
  disable_down = bool(task_config.get("disable_down", False))
  disable_left = bool(task_config.get("disable_left", False))
  disable_right = bool(task_config.get("disable_right", False))
  control_mode = str(task_config.get("control_mode", "cumulative"))
  cursor_only_mode = bool(task_config.get("cursor_only_mode", False))
  free_play_end_key = str(task_config.get("free_play_end_key", "space"))
  cumulative_speed = float(task_config.get("cumulative_speed", 0.70))
  zero_drift_mode = bool(task_config.get("zero_drift_mode", True))
  zero_drift_buffer = float(task_config.get("zero_drift_buffer", 0.03))
  direct_range = float(task_config.get("direct_range", 0.45))
  cursor_diameter_ratio = float(task_config.get("cursor_diameter_ratio", 0.03))
  cursor_color = QColor(*task_config.get("cursor_color", [255, 70, 70]))
  task_region_x = float(task_config.get("task_region_x", 0.5))
  task_region_y = float(task_config.get("task_region_y", 0.5))
  task_region_width = float(task_config.get("task_region_width", 1.0))
  task_region_height = float(task_config.get("task_region_height", 1.0))
  reward_channel = int(task_config.get("reward_channel", 0))
  target_radius_ratio = DEFAULT_TARGET_RADIUS_RATIO
  target_color = QColor(*DEFAULT_TARGET_COLOR)
  hold_time = DEFAULT_TARGET_HOLD_TIME
  trial_timeout = float(task_config.get("trial_timeout", 10.0))
  intertrial_interval = float(task_config.get("intertrial_interval", 1.0))
  configured_targets = task_config.get("targets", [])

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

  def renderer(painter: CanvasPainterProtocol) -> None:
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
      painter.setPen(QPen(current_target_color, 1))
      painter.setBrush(current_target_color)
      painter.drawEllipse(tx - target_radius_px, ty - target_radius_px, 2 * target_radius_px, 2 * target_radius_px)

    r = cursor_diameter_px // 2
    painter.setPen(QPen(cursor_color, 1))
    painter.setBrush(cursor_color)
    painter.drawEllipse(cx - r, cy - r, cursor_diameter_px, cursor_diameter_px)

    painter.setPen(QPen(QColor(255, 255, 255), 1))
    status_text = f"Mode: {control_mode}"
    if cursor_only_mode:
      status_text += f"  Free Play (press {free_play_end_key.upper()} to end)"
    painter.drawText(10, 20, status_text)

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
        jx = joystick_x
        jy = joystick_y
        if disable_right and jx > 0.0:
          jx = 0.0
        if disable_left and jx < 0.0:
          jx = 0.0
        if disable_up and jy > 0.0:
          jy = 0.0
        if disable_down and jy < 0.0:
          jy = 0.0
        cursor_x = 0.5 + jx * direct_range
        cursor_y = 0.5 + jy * direct_range
      else:
        jx = joystick_x
        jy = joystick_y
        if disable_right and jx > 0.0:
          jx = 0.0
        if disable_left and jx < 0.0:
          jx = 0.0
        if disable_up and jy > 0.0:
          jy = 0.0
        if disable_down and jy < 0.0:
          jy = 0.0
        if zero_drift_mode:
          # Radial deadband to suppress small spring-back offsets near center.
          if math.hypot(jx, jy) < zero_drift_buffer:
            jx = 0.0
            jy = 0.0
        cursor_x += jx * cumulative_speed * dt
        cursor_y += jy * cumulative_speed * dt

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

      if cursor_only_mode:
        if free_play_end_requested:
          await context.log("BehavState=success")
          return TaskResult(success=True)
      elif state == "iti":
        if now >= iti_end:
          target_x, target_y, current_target_radius_ratio, current_hold_time, current_target_color = place_target()
          hold_start = None
          trial_start = now
          state = "active"
          await context.log("BehavState=active")
      else:
        dist_to_target = math.hypot(cursor_px_x - target_px_x, cursor_px_y - target_px_y)
        if dist_to_target <= target_radius_px:
          if hold_start is None:
            hold_start = now
          elif now - hold_start >= current_hold_time:
            await deliver_reward()
            await context.log("BehavState=success")
            return TaskResult(success=True)
        else:
          hold_start = None

        if now - trial_start >= trial_timeout:
          await context.log("BehavState=fail")
          return TaskResult(success=False)

      context.widget.update()
      await asyncio.sleep(0.01)
  finally:
    analog_task.cancel()
    try:
      await analog_task
    except Exception:
      pass
