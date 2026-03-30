"""
Joystick paddle intro task.

Goal:
- Bridge joystick-to-cursor agency into Pong-like interception.
- Subject controls a vertical paddle on the left side of the task region.
- A ball approaches from the right and must be intercepted for reward.

Full task description, definitions, and overall design can be read in 
docs/joystick_paddle_intro_task_guide.md. Any changes done to this task should be 
reflected on the accompanying md file

Control modes:
- direct: joystick Y directly maps paddle Y around the center
- cumulative: joystick Y acts like velocity input for the paddle

Operator testing:
- Arrow keys can drive the paddle during testing.
- Up/Down move the paddle; Left/Right are tracked for consistency with the
  existing joystick intro task listeners.
"""

import asyncio
import datetime
import logging
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

KEYBOARD_JOYSTICK_MAGNITUDE = 1.0
OPERATOR_KEYBOARD_PADDLE_SPEED = 0.85

DEFAULT_PADDLE_COLOR = [80, 220, 255]
DEFAULT_BALL_COLOR = [255, 210, 90]
DEFAULT_INTERCEPT_COLOR = [120, 120, 120]


def toggle_brightness(brightness: int) -> int:
  return 0 if brightness == 255 else 255


def clamp(value: float, lo: float, hi: float) -> float:
  return max(lo, min(hi, value))


def create_widget(task_config: ObservableCollection) -> QWidget:
  class NoWheelChangeFilter(QWidget):
    def eventFilter(self, watched: typing.Any, event: typing.Any) -> bool:
      if hasattr(event, "angleDelta"):
        if isinstance(watched, (QSpinBox, QDoubleSpinBox, QComboBox, QSlider)):
          event.ignore()
          return True
      return super().eventFilter(watched, event)

  if "joystick_node" not in task_config:
    task_config["joystick_node"] = "Joystick"
  if "control_mode" not in task_config:
    task_config["control_mode"] = "direct"
  if "cumulative_speed" not in task_config:
    task_config["cumulative_speed"] = 0.75
  if "zero_drift_mode" not in task_config:
    task_config["zero_drift_mode"] = True
  if "zero_drift_buffer" not in task_config:
    task_config["zero_drift_buffer"] = 0.05
  if "direct_range" not in task_config:
    task_config["direct_range"] = 0.44
  if "direct_recenter_when_idle" not in task_config:
    task_config["direct_recenter_when_idle"] = False
  if "reset_paddle_each_trial" not in task_config:
    task_config["reset_paddle_each_trial"] = False
  if "task_region_x" not in task_config:
    task_config["task_region_x"] = 0.5
  if "task_region_y" not in task_config:
    task_config["task_region_y"] = 0.5
  if "task_region_width" not in task_config:
    task_config["task_region_width"] = 0.72
  if "task_region_height" not in task_config:
    task_config["task_region_height"] = 0.78
  if "reward_channel" not in task_config:
    task_config["reward_channel"] = 0
  if "intertrial_interval" not in task_config:
    task_config["intertrial_interval"] = 1.0
  if "trial_timeout" not in task_config:
    task_config["trial_timeout"] = 4.0
  if "ignore_idle_trial_failures" not in task_config:
    task_config["ignore_idle_trial_failures"] = True
  if "paddle_x_norm" not in task_config:
    task_config["paddle_x_norm"] = 0.12
  if "paddle_height_ratio" not in task_config:
    task_config["paddle_height_ratio"] = 0.22
  if "paddle_width_ratio" not in task_config:
    task_config["paddle_width_ratio"] = 0.035
  if "paddle_color" not in task_config:
    task_config["paddle_color"] = DEFAULT_PADDLE_COLOR.copy()
  if "ball_radius_ratio" not in task_config:
    task_config["ball_radius_ratio"] = 0.04
  if "ball_color" not in task_config:
    task_config["ball_color"] = DEFAULT_BALL_COLOR.copy()
  if "intercept_line_color" not in task_config:
    task_config["intercept_line_color"] = DEFAULT_INTERCEPT_COLOR.copy()
  if "ball_start_x_norm" not in task_config:
    task_config["ball_start_x_norm"] = 0.92
  if "ball_speed_norm_per_s" not in task_config:
    task_config["ball_speed_norm_per_s"] = 0.52
  if "ball_vertical_drift_norm_per_s" not in task_config:
    task_config["ball_vertical_drift_norm_per_s"] = 0.12
  if "ball_spawn_y_min" not in task_config:
    task_config["ball_spawn_y_min"] = 0.18
  if "ball_spawn_y_max" not in task_config:
    task_config["ball_spawn_y_max"] = 0.82
  if "ball_bounce_off_walls" not in task_config:
    task_config["ball_bounce_off_walls"] = False
  if "show_intercept_line" not in task_config:
    task_config["show_intercept_line"] = True
  if "show_trail" not in task_config:
    task_config["show_trail"] = True
  if "success_flash_duration_s" not in task_config:
    task_config["success_flash_duration_s"] = 0.15
  if "state_indicator_x" not in task_config:
    task_config["state_indicator_x"] = 30
  if "state_indicator_y" not in task_config:
    task_config["state_indicator_y"] = 70

  result = QWidget()
  layout = QVBoxLayout(result)
  result.setLayout(layout)

  form = Form.build(
    task_config, ["Parameter", "Value"],
    Form.String("Joystick Node", "joystick_node", "Joystick"),
    Form.Choice("Control Mode", "control_mode", [
      ("Direct", "direct"),
      ("Cumulative", "cumulative"),
    ]),
    Form.Constant("Cumulative Speed", "cumulative_speed", 0.75, precision=3),
    Form.Bool("Zero-Drift Mode", "zero_drift_mode", True),
    Form.Constant("Zero-Drift Buffer", "zero_drift_buffer", 0.05, precision=3),
    Form.Constant("Direct Range", "direct_range", 0.44, precision=3),
    Form.Bool("Direct Recenter When Idle", "direct_recenter_when_idle", False),
    Form.Bool("Reset Paddle Each Trial", "reset_paddle_each_trial", False),
    Form.Constant("Task Region Center X", "task_region_x", 0.5, precision=3),
    Form.Constant("Task Region Center Y", "task_region_y", 0.5, precision=3),
    Form.Constant("Task Region Width", "task_region_width", 0.72, precision=3),
    Form.Constant("Task Region Height", "task_region_height", 0.78, precision=3),
    Form.Constant("Paddle X", "paddle_x_norm", 0.12, precision=3),
    Form.Constant("Paddle Height", "paddle_height_ratio", 0.22, precision=3),
    Form.Constant("Paddle Width", "paddle_width_ratio", 0.035, precision=3),
    Form.Color("Paddle Color", "paddle_color", QColor(*DEFAULT_PADDLE_COLOR)),
    Form.Constant("Ball Radius", "ball_radius_ratio", 0.04, precision=3),
    Form.Color("Ball Color", "ball_color", QColor(*DEFAULT_BALL_COLOR)),
    Form.Constant("Ball Start X", "ball_start_x_norm", 0.92, precision=3),
    Form.Constant("Ball Speed", "ball_speed_norm_per_s", 0.52, precision=3),
    Form.Constant("Ball Vertical Drift", "ball_vertical_drift_norm_per_s", 0.12, precision=3),
    Form.Constant("Ball Spawn Y Min", "ball_spawn_y_min", 0.18, precision=3),
    Form.Constant("Ball Spawn Y Max", "ball_spawn_y_max", 0.82, precision=3),
    Form.Bool("Ball Bounce Off Walls", "ball_bounce_off_walls", False),
    Form.Bool("Show Intercept Line", "show_intercept_line", True),
    Form.Bool("Show Trail", "show_trail", True),
    Form.Color("Intercept Line Color", "intercept_line_color", QColor(*DEFAULT_INTERCEPT_COLOR)),
    Form.Constant("Reward Channel", "reward_channel", 0, precision=0),
    Form.Constant("Intertrial Interval (s)", "intertrial_interval", 1.0, "s", precision=3),
    Form.Constant("Trial Timeout (s)", "trial_timeout", 4.0, "s", precision=3),
    Form.Bool("Ignore Idle Trial Failures", "ignore_idle_trial_failures", True),
    Form.Constant("Success Flash (s)", "success_flash_duration_s", 0.15, "s", precision=3),
    Form.Constant("State Indicator Right Margin", "state_indicator_x", 30, precision=0),
    Form.Constant("State Indicator Bottom Margin", "state_indicator_y", 70, precision=0),
  )
  layout.addWidget(form)

  instructions = QLabel(
    "Pre-Pong shaping: control the left paddle, intercept the incoming ball, "
    "and earn reward on contact. Arrow keys also control the paddle for testing."
  )
  instructions.setWordWrap(True)
  layout.addWidget(instructions)

  no_wheel_filter = NoWheelChangeFilter(result)
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
  control_mode = str(task_config.get("control_mode", "direct"))
  cumulative_speed = float(task_config.get("cumulative_speed", 0.75))
  zero_drift_mode = bool(task_config.get("zero_drift_mode", True))
  zero_drift_buffer = float(task_config.get("zero_drift_buffer", 0.05))
  direct_range = float(task_config.get("direct_range", 0.44))
  direct_recenter_when_idle = bool(task_config.get("direct_recenter_when_idle", False))
  reset_paddle_each_trial = bool(task_config.get("reset_paddle_each_trial", False))
  task_region_x = clamp(float(task_config.get("task_region_x", 0.5)), 0.0, 1.0)
  task_region_y = clamp(float(task_config.get("task_region_y", 0.5)), 0.0, 1.0)
  task_region_width = clamp(float(task_config.get("task_region_width", 0.72)), 0.05, 1.0)
  task_region_height = clamp(float(task_config.get("task_region_height", 0.78)), 0.05, 1.0)
  reward_channel = max(0, int(task_config.get("reward_channel", 0)))
  intertrial_interval = max(0.0, float(task_config.get("intertrial_interval", 1.0)))
  trial_timeout = max(0.1, float(task_config.get("trial_timeout", 4.0)))
  ignore_idle_trial_failures = bool(task_config.get("ignore_idle_trial_failures", True))
  paddle_x_norm = clamp(float(task_config.get("paddle_x_norm", 0.12)), 0.02, 0.45)
  paddle_height_ratio = clamp(float(task_config.get("paddle_height_ratio", 0.22)), 0.05, 0.6)
  paddle_width_ratio = clamp(float(task_config.get("paddle_width_ratio", 0.035)), 0.01, 0.12)
  paddle_color = QColor(*task_config.get("paddle_color", DEFAULT_PADDLE_COLOR))
  ball_radius_ratio = clamp(float(task_config.get("ball_radius_ratio", 0.04)), 0.01, 0.2)
  ball_color = QColor(*task_config.get("ball_color", DEFAULT_BALL_COLOR))
  intercept_line_color = QColor(*task_config.get("intercept_line_color", DEFAULT_INTERCEPT_COLOR))
  ball_start_x_norm = clamp(float(task_config.get("ball_start_x_norm", 0.92)), 0.5, 0.98)
  ball_speed_norm_per_s = max(0.05, float(task_config.get("ball_speed_norm_per_s", 0.52)))
  ball_vertical_drift_norm_per_s = max(0.0, float(task_config.get("ball_vertical_drift_norm_per_s", 0.12)))
  ball_spawn_y_min = clamp(float(task_config.get("ball_spawn_y_min", 0.18)), 0.0, 1.0)
  ball_spawn_y_max = clamp(float(task_config.get("ball_spawn_y_max", 0.82)), 0.0, 1.0)
  if ball_spawn_y_min > ball_spawn_y_max:
    ball_spawn_y_min, ball_spawn_y_max = ball_spawn_y_max, ball_spawn_y_min
  ball_bounce_off_walls = bool(task_config.get("ball_bounce_off_walls", False))
  show_intercept_line = bool(task_config.get("show_intercept_line", True))
  show_trail = bool(task_config.get("show_trail", True))
  success_flash_duration_s = clamp(float(task_config.get("success_flash_duration_s", 0.15)), 0.0, 1.0)
  state_indicator_x = max(0, int(task_config.get("state_indicator_x", 30)))
  state_indicator_y = max(0, int(task_config.get("state_indicator_y", 70)))

  session_start = time.perf_counter()
  paddle_y = 0.5 if reset_paddle_each_trial else float(task_config.get("_last_paddle_y", 0.5))
  paddle_y = clamp(paddle_y, 0.0, 1.0)
  ball_x = ball_start_x_norm
  ball_y = 0.5
  ball_vx = -ball_speed_norm_per_s
  ball_vy = 0.0
  ball_contacted = False
  flash_until: typing.Optional[float] = None

  trial_start = session_start
  last_tick = session_start
  state = "intertrial"
  iti_end = trial_start + intertrial_interval
  state_brightness = 0
  analog_joystick_x = 0.0
  analog_joystick_y = 0.0
  operator_left_pressed = get_persisted_operator_key_state("left") if not reset_paddle_each_trial else False
  operator_right_pressed = get_persisted_operator_key_state("right") if not reset_paddle_each_trial else False
  operator_up_pressed = get_persisted_operator_key_state("up") if not reset_paddle_each_trial else False
  operator_down_pressed = get_persisted_operator_key_state("down") if not reset_paddle_each_trial else False
  operator_paddle_latched = False
  joystick_active_this_trial = False
  first_movement_time: typing.Optional[float] = None
  trial_index = 0
  current_attempt: typing.Optional[typing.Dict[str, typing.Any]] = None
  behav_result: typing.Dict[str, typing.Any] = {
    "task": "joystick_paddle_intro",
    "control_mode": control_mode,
    "trial_attempt_count": 0,
    "attempts": [],
    "joystick_samples": [],
    "session_start_perf_counter": session_start,
    "final_outcome": None,
  }
  context.behav_result = behav_result

  def region_bounds_ratios() -> typing.Tuple[float, float, float, float]:
    left = task_region_x - (task_region_width / 2.0)
    top = task_region_y - (task_region_height / 2.0)
    left = clamp(left, 0.0, 1.0 - task_region_width)
    top = clamp(top, 0.0, 1.0 - task_region_height)
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

  def on_key_press(event: QKeyEvent) -> None:
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
            behav_result["joystick_samples"].append({
              "time_perf_counter": sample_time,
              "time_since_session_start_s": max(0.0, sample_time - session_start),
              "x": sample_x,
              "y": sample_y,
            })
            analog_joystick_x = sample_x
            analog_joystick_y = sample_y
        elif len(message.data) >= 2:
          analog_joystick_x = float(message.data[0])
          analog_joystick_y = float(message.data[1])
          behav_result["joystick_samples"].append({
            "time_perf_counter": sample_received_at,
            "time_since_session_start_s": max(0.0, sample_received_at - session_start),
            "x": analog_joystick_x,
            "y": analog_joystick_y,
          })
    finally:
      stream.cancel()

  def get_operator_joystick() -> typing.Tuple[float, float]:
    x = float(operator_right_pressed) - float(operator_left_pressed)
    y = float(operator_up_pressed) - float(operator_down_pressed)
    return (
      clamp(x * KEYBOARD_JOYSTICK_MAGNITUDE, -1.0, 1.0),
      clamp(y * KEYBOARD_JOYSTICK_MAGNITUDE, -1.0, 1.0),
    )

  async def deliver_reward(channel_index: int) -> None:
    on_time_ms = int(context.get_reward(channel_index))
    if on_time_ms <= 0:
      LOGGER.info("Reward skipped: channel=%d returned %d ms", channel_index, on_time_ms)
      return
    signal = thalamus_pb2.AnalogResponse(
      data=[5, 0],
      spans=[thalamus_pb2.Span(begin=0, end=2, name="Reward")],
      sample_intervals=[1_000_000 * on_time_ms],
    )
    LOGGER.info("Delivering reward channel=%d duration_ms=%d", channel_index, on_time_ms)
    await context.inject_analog("Reward", signal)

  def reset_attempt_tracking(now: float) -> None:
    nonlocal current_attempt, trial_index, first_movement_time
    trial_index += 1
    first_movement_time = None
    current_attempt = {
      "attempt_index": trial_index,
      "start_time_perf_counter": now,
      "control_mode": control_mode,
      "paddle_x_norm": paddle_x_norm,
      "paddle_height_ratio": paddle_height_ratio,
      "ball_radius_ratio": ball_radius_ratio,
      "ball_speed_norm_per_s": ball_speed_norm_per_s,
      "events": [],
      "joystick_active": False,
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
    current_attempt["end_time_perf_counter"] = now
    current_attempt["duration_s"] = max(0.0, now - float(current_attempt["start_time_perf_counter"]))
    current_attempt["first_movement_time_s"] = None if first_movement_time is None else max(
      0.0, first_movement_time - float(current_attempt["start_time_perf_counter"]))
    behav_result["attempts"].append(current_attempt)
    behav_result["trial_attempt_count"] = len(behav_result["attempts"])
    behav_result["final_outcome"] = outcome
    behav_result["final_attempt"] = current_attempt
    context.behav_result = behav_result

  def start_new_trial(now: float) -> None:
    nonlocal ball_x, ball_y, ball_vx, ball_vy, ball_contacted, trial_start, state, state_brightness, joystick_active_this_trial
    ball_x = ball_start_x_norm
    ball_y = random.uniform(ball_spawn_y_min, ball_spawn_y_max)
    ball_vx = -ball_speed_norm_per_s
    ball_vy = random.uniform(-ball_vertical_drift_norm_per_s, ball_vertical_drift_norm_per_s)
    ball_contacted = False
    trial_start = now
    joystick_active_this_trial = False
    reset_attempt_tracking(now)
    state = "start_on"
    state_brightness = toggle_brightness(state_brightness)
    if current_attempt is not None:
      current_attempt["ball_start_position"] = {"x_norm": ball_x, "y_norm": ball_y}
      current_attempt["ball_velocity"] = {"x_norm_per_s": ball_vx, "y_norm_per_s": ball_vy}
    append_event("ball_on", now, ball_x=ball_x, ball_y=ball_y, ball_vx=ball_vx, ball_vy=ball_vy)

  def renderer(painter: CanvasPainterProtocol) -> None:
    w = context.widget.width()
    h = context.widget.height()
    region_left, region_top, region_w, region_h = region_bounds_ratios()
    left_px = int(region_left * w)
    top_px = int(region_top * h)
    region_w_px = max(1, int(region_w * w))
    region_h_px = max(1, int(region_h * h))
    min_dim = min(region_w_px, region_h_px)

    paddle_w_px = max(4, int(paddle_width_ratio * region_w_px))
    paddle_h_px = max(8, int(paddle_height_ratio * region_h_px))
    ball_r_px = max(4, int(ball_radius_ratio * min_dim))
    paddle_cx_px, paddle_cy_px = to_region_pixels(paddle_x_norm, paddle_y, w, h)
    ball_px_x, ball_px_y = to_region_pixels(ball_x, ball_y, w, h)
    paddle_left = paddle_cx_px - paddle_w_px // 2
    paddle_top = paddle_cy_px - paddle_h_px // 2

    painter.setPen(QPen(QColor(120, 120, 120), 1))
    painter.setBrush(Qt.BrushStyle.NoBrush)
    painter.drawRect(left_px, top_px, region_w_px, region_h_px)

    if show_intercept_line:
      painter.setPen(QPen(intercept_line_color, 1, Qt.PenStyle.DashLine))
      painter.drawLine(paddle_cx_px, top_px, paddle_cx_px, top_px + region_h_px)

    if show_trail and state == "start_on":
      painter.setPen(QPen(QColor(90, 90, 90), 1, Qt.PenStyle.DotLine))
      painter.drawLine(ball_px_x, ball_px_y, paddle_cx_px, ball_px_y)

    painter.setPen(QPen(paddle_color, 1))
    painter.setBrush(paddle_color)
    painter.drawRect(paddle_left, paddle_top, paddle_w_px, paddle_h_px)

    if state == "start_on":
      painter.setPen(QPen(ball_color, 1))
      painter.setBrush(ball_color)
      painter.drawEllipse(ball_px_x - ball_r_px, ball_px_y - ball_r_px, 2 * ball_r_px, 2 * ball_r_px)

    flash_active = (
      state == "start_on"
      and flash_until is not None
      and time.perf_counter() < flash_until
    )
    if flash_active:
      painter.setPen(QPen(QColor(255, 255, 255, 200), max(2, int(0.008 * min_dim))))
      painter.setBrush(Qt.BrushStyle.NoBrush)
      flash_radius = int(ball_r_px * 1.8)
      painter.drawEllipse(ball_px_x - flash_radius, ball_px_y - flash_radius, 2 * flash_radius, 2 * flash_radius)

    painter.setPen(QPen(QColor(255, 255, 255), 1))
    painter.drawText(10, 20, f"Mode: {control_mode}  Paddle Intro")
    painter.drawText(10, 40, "Goal: align paddle with incoming ball")
    painter.drawText(10, 60, "Operator test: Up/Down arrows")

    state_color = QColor(state_brightness, state_brightness, state_brightness)
    state_width = 70
    painter.fillRect(
      w - state_width - state_indicator_x,
      h - state_width - state_indicator_y,
      state_width,
      state_width,
      state_color,
    )

  context.widget.renderer = renderer
  context.widget.key_release_handler = on_key_release
  context.widget.key_press_handler = on_key_press
  context.widget.setFocus()

  channel = context.get_channel("localhost:50050")
  stub = thalamus_pb2_grpc.ThalamusStub(channel)
  request = thalamus_pb2.AnalogRequest(
    node=thalamus_pb2.NodeSelector(name=joystick_node),
    channel_names=["X", "Y"],
  )
  stream = stub.analog(request)
  analog_task = create_task_with_exc_handling(analog_processor(stream))

  try:
    await context.log("BehavState=intertrial")
    while True:
      now = time.perf_counter()
      dt = max(0.0, min(0.05, now - last_tick))
      last_tick = now

      _, operator_jy = get_operator_joystick()
      operator_override_active = operator_jy != 0.0
      analog_active = abs(analog_joystick_y) >= (zero_drift_buffer if zero_drift_mode else 0.02)
      jy = 0.0

      if operator_override_active:
        jy = operator_jy
        paddle_y += jy * OPERATOR_KEYBOARD_PADDLE_SPEED * dt
        operator_paddle_latched = True
      elif operator_paddle_latched and not analog_active:
        jy = 0.0
      elif control_mode == "direct":
        jy = analog_joystick_y
        if analog_active or direct_recenter_when_idle:
          paddle_y = 0.5 + jy * direct_range
        operator_paddle_latched = False
      else:
        jy = analog_joystick_y
        if zero_drift_mode and abs(jy) < zero_drift_buffer:
          jy = 0.0
        paddle_y += jy * cumulative_speed * dt
        operator_paddle_latched = False

      joystick_motion_threshold = zero_drift_buffer if zero_drift_mode else 0.02
      joystick_is_active = abs(jy) >= joystick_motion_threshold
      if joystick_is_active and not joystick_active_this_trial:
        first_movement_time = now
        append_event("first_joystick_movement", now, joystick_y=jy)
      if joystick_is_active:
        joystick_active_this_trial = True
        if current_attempt is not None:
          current_attempt["joystick_active"] = True

      half_paddle = paddle_height_ratio / 2.0
      paddle_y = clamp(paddle_y, half_paddle, 1.0 - half_paddle)

      if state == "intertrial":
        if now >= iti_end:
          start_new_trial(now)
          await context.log("BehavState=start_on")
      else:
        previous_ball_x = ball_x
        ball_x += ball_vx * dt
        ball_y += ball_vy * dt

        if ball_bounce_off_walls:
          if ball_y <= ball_radius_ratio:
            ball_y = ball_radius_ratio
            ball_vy = abs(ball_vy)
            append_event("wall_bounce_top", now, ball_y=ball_y)
          elif ball_y >= 1.0 - ball_radius_ratio:
            ball_y = 1.0 - ball_radius_ratio
            ball_vy = -abs(ball_vy)
            append_event("wall_bounce_bottom", now, ball_y=ball_y)
        else:
          ball_y = clamp(ball_y, ball_radius_ratio, 1.0 - ball_radius_ratio)

        paddle_half_h = paddle_height_ratio / 2.0
        paddle_half_w = paddle_width_ratio / 2.0
        paddle_left_x = paddle_x_norm - paddle_half_w
        paddle_right_x = paddle_x_norm + paddle_half_w
        paddle_top_y = paddle_y + paddle_half_h
        paddle_bottom_y = paddle_y - paddle_half_h

        crossed_paddle_face = previous_ball_x >= paddle_left_x and ball_x <= paddle_right_x
        overlaps_paddle_y = (ball_y + ball_radius_ratio) >= paddle_bottom_y and (ball_y - ball_radius_ratio) <= paddle_top_y

        if crossed_paddle_face and overlaps_paddle_y and not ball_contacted:
          ball_contacted = True
          append_event("intercept", now, paddle_y=paddle_y, ball_y=ball_y)
          if success_flash_duration_s > 0.0:
            flash_until = now + success_flash_duration_s
            while time.perf_counter() < flash_until:
              context.widget.update()
              await context.sleep(datetime.timedelta(seconds=0.01))
          await deliver_reward(reward_channel)
          append_event("reward_triggered", now, reward_channel=reward_channel)
          append_event("success", now)
          finalize_attempt("success", now)
          await context.log("BehavState=success")
          return TaskResult(success=True)

        if (ball_x + ball_radius_ratio) < paddle_left_x:
          failure_reason = "missed_ball_after_movement" if joystick_active_this_trial else "missed_ball_without_movement"
          append_event("miss", now, paddle_y=paddle_y, ball_y=ball_y, failure_reason=failure_reason)
          finalize_attempt(
            "ignored_idle" if ignore_idle_trial_failures and not joystick_active_this_trial else "fail",
            now,
            failure_reason=failure_reason,
          )
          if ignore_idle_trial_failures and not joystick_active_this_trial:
            state = "intertrial"
            iti_end = now + intertrial_interval
            state_brightness = 0
            await context.log("BehavState=intertrial")
            continue
          await context.log("BehavState=fail")
          return TaskResult(success=False)

        if now - trial_start >= trial_timeout:
          failure_reason = "timeout_without_movement" if not joystick_active_this_trial else "timeout_after_movement"
          append_event("fail", now, failure_reason=failure_reason)
          finalize_attempt(
            "ignored_idle" if ignore_idle_trial_failures and not joystick_active_this_trial else "fail",
            now,
            failure_reason=failure_reason,
          )
          if ignore_idle_trial_failures and not joystick_active_this_trial:
            state = "intertrial"
            iti_end = now + intertrial_interval
            state_brightness = 0
            await context.log("BehavState=intertrial")
            continue
          await context.log("BehavState=fail")
          return TaskResult(success=False)

      context.widget.update()
      await context.sleep(datetime.timedelta(seconds=0.01))
  finally:
    task_config["_last_paddle_y"] = float(paddle_y)
    persist_operator_key_state()
    analog_task.cancel()
    try:
      await analog_task
    except asyncio.CancelledError:
      pass
    except Exception:
      pass
