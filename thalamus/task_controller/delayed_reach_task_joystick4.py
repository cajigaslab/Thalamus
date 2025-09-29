"""
Implementation of the delayed center-out reach task, joystick‐driven circle version

at home (windows)
    > .\venv\Scripts\activate.ps1
linux:
    > source .venv/bin/activate
> python -m thalamus.task_controller --pypipeline -c _.json

Comments:
This task is working base of the joystick center out task using velocity cursor.
"""

import typing
import serial
import time
import re
import logging
import datetime
import asyncio
import math
import random

from ..qt import *
from . import task_context
from .. import thalamus_pb2
from .widgets import Form
from .util import create_task_with_exc_handling, TaskResult, TaskContextProtocol, CanvasPainterProtocol
from ..config import *

LOGGER = logging.getLogger(__name__)

# ─────── JOYSTICK PARAMS ───────
rig_port =  '/dev/ttyACM0'  # check picture
mac_port = '/dev/cu.usbmodem313301'     # right hand side with dongle
home_pc = 'COM3'

SERIAL_PORT = home_pc
BAUD_RATE = 115200
DEAD_ZONE = 10
MID = 512.0
# ────────────────────────────────
pattern = re.compile(r"x\s*=\s*(\d+)\s*,\s*y\s*=\s*(\d+)")


def normalize(value: int) -> float:
    if abs(value - MID) < DEAD_ZONE:
        value = MID
    if value < 0:
        value = 0
    elif value > 1023:
        value = 1023
    return (value - MID) / MID


def create_widget(task_config: ObservableCollection) -> QWidget:
    result = QWidget()
    layout = QVBoxLayout(result)
    result.setLayout(layout)

    form = Form.build(
        task_config, ["Parameter", "Value"],
        # cursor settings
        Form.Constant("Cursor Speed", "cursor_speed", 0.003, precision=5),
        Form.Constant("Cursor Diameter Ratio", "cursor_diameter_ratio", 0.03),
        Form.Color("Cursor Color", 'cursor_color', QColor(255, 0, 0)),
        # default 8-target
        Form.Bool("Use Default 8-Target Layout", "use_default_layout", False),
        Form.Constant("Target Hold Time (s)", "hold_time", 1.0, "s"),
        # others
        Form.Constant("Auto-Fail Period (s)", 'autofail', 5, "s"),
        Form.Constant("Fixation Hold Time (s)", "fixation_hold", 0.5, "s"),
        Form.Constant("Delay Hold Time (s)", 'delay_hold', 0.1, "s"),
        Form.Constant("Intertrial Interval (s)", "intertrial_interval", 1, "s"),
        Form.Constant("Fail Interval (s)", "fail_interval", 1.5, "s"),
        Form.Constant("Success Interval (s)", "success_interval", 1, "s"),
        # targets
        Form.Table("Custom Targets", "custom_targets",
                   ["hold_time",
                    "target_radius_ratio",
                    "target_distance_ratio",
                    "acceptance_ratio",
                    "angle_deg",
                    "target_color"]),
    )
    layout.addWidget(form)
    return result


READING_JOYSTICK = False
# initializing 'cursor' in center of screen
cursor_x = 0.5
cursor_y = 0.5
joystick_x = 0.0
joystick_y = 0.0


async def run(context: TaskContextProtocol) -> TaskResult:
    assert context.widget, 'Widget is None; cannot render.'
    global READING_JOYSTICK, cursor_x, cursor_y, joystick_x, joystick_y

    task_config = context.config["queue"][0]
    hold_time = task_config.get("hold_time", 1.0)
    fixation_hold = task_config.get("fixation_hold", 0.5)
    target_radius_ratio = task_config.get("target_radius_ratio", 0.05)
    target_distance_ratio = task_config.get("target_distance_ratio", 0.3)
    target_color = QColor(*task_config.get("target_color", [0, 255, 0]))
    cursor_diameter_ratio = task_config.get("cursor_diameter_ratio", 0.03)
    cursor_color = QColor(*task_config.get("cursor_color", [255, 0, 0]))
    use_default_layout = task_config.get("use_default_layout", True)
    custom_targets = task_config.get("custom_targets", [])
    cursor_speed = task_config.get("cursor_speed", 0.01)
    autofail = task_config.get("autofail", 5.0)
    delay_hold = task_config.get("delay_hold", 0.1)
    intertrial_interval = task_config.get("intertrial_interval", 1.0)
    fail_interval = task_config.get("fail_interval", 1.5)
    success_interval = task_config.get("success_interval", 1.0)

    state = "fixation"
    state_timer = time.time()

    target_index = 0
    target_pos = (0, 0)
    cursor_hold_start = None
    fixation_hold_start = None
    num_targets = 8 if use_default_layout else len(custom_targets)

    target_radius_px = 0
    target_color_cur = QColor(0, 255, 0)

    delay_hold_start = None
    trial_start_time = None
    iti_end_time = None
    pending_result = None

    def get_target_position(index: int, width: int, height: int):
        cx, cy = width // 2, height // 2

        if use_default_layout:
            # evenly spaced around circle
            angle = 2 * math.pi * index / num_targets
            distance_ratio = target_distance_ratio
            radius_ratio = target_radius_ratio
            col = target_color
            acceptance = 1.0
        else:
            # per-target parameters
            t = custom_targets[index]
            distance_ratio = float(t.get("target_distance_ratio", target_distance_ratio))
            radius_ratio = float(t.get("target_radius_ratio", target_radius_ratio))
            acceptance = float(t.get("acceptance_ratio", 1.0))
            angle_deg = float(t.get("angle_deg", 360.0 * index / num_targets))
            col_list = t.get("target_color", [0, 255, 0])
            col = QColor(*col_list)
            angle = math.radians(angle_deg)

        # --- unified placement: scale separately for X and Y ---
        tx = cx + (distance_ratio * (width / 2)) * math.cos(angle)
        ty = cy - (distance_ratio * (height / 2)) * math.sin(angle)

        # target size based on min dimension
        r_px = int(radius_ratio * min(width, height))

        return (int(tx), int(ty)), r_px, col, acceptance

    def renderer(painter: CanvasPainterProtocol) -> None:
        w = context.widget.width()
        h = context.widget.height()

        # cursor position (normalized → pixels)
        cx = int(cursor_x * w)
        cy = int((1.0 - cursor_y) * h)

        min_dim = min(w, h)
        cursor_diameter = int(cursor_diameter_ratio * min_dim)

        # Draw fixation cross in fixation & delay
        if state in ("fixation", "delay"):
            painter.setPen(QPen(QColor(255, 255, 255), 2))
            center_x, center_y = w // 2, h // 2
            painter.drawLine(center_x - 10, center_y, center_x + 10, center_y)
            painter.drawLine(center_x, center_y - 10, center_x, center_y + 10)

        # Draw target in delay & active
        if state in ("delay", "active"):
            painter.setPen(QPen(target_color_cur, 1))
            painter.setBrush(target_color_cur)
            painter.drawEllipse(target_pos[0] - target_radius_px,
                                target_pos[1] - target_radius_px,
                                2 * target_radius_px, 2 * target_radius_px)

        # Draw cursor (always)
        r = cursor_diameter // 2
        painter.setPen(QPen(cursor_color, 1))
        painter.setBrush(cursor_color)
        painter.drawEllipse(cx - r, cy - r, cursor_diameter, cursor_diameter)

    context.widget.renderer = renderer

    async def poll_joystick_from_serial():
        global joystick_x, joystick_y
        last_write = time.perf_counter()

        while True:
            raw_bytes = ser.readline()
            if raw_bytes:
                raw_line = raw_bytes.decode("utf-8", errors="ignore").strip()
                m = pattern.search(raw_line)
                if m:
                    raw_x = int(m.group(1))
                    raw_y = int(m.group(2))
                    joystick_x = normalize(raw_x)
                    joystick_y = normalize(raw_y)
                    now = time.perf_counter()
                    if now - last_write >= .01:
                        last_write = now
                        await context.inject_analog("joystick", thalamus_pb2.AnalogResponse(
                            data = [joystick_x, joystick_y],
                            sample_intervals=[0, 0],
                            spans=[
                                thalamus_pb2.Span(name="X",begin=0, end=1),
                                thalamus_pb2.Span(name="Y",begin=1, end=2)
                            ]
                        ))
            await asyncio.sleep(0)

    async def trial_loop():
        nonlocal state, state_timer, target_index, target_pos, target_radius_px, target_color_cur
        nonlocal cursor_hold_start, fixation_hold_start, delay_hold_start
        nonlocal trial_start_time, iti_end_time, pending_result
        global cursor_x, cursor_y, joystick_x, joystick_y

        while True:
            w, h = context.widget.width(), context.widget.height()

            # integrate joystick velocity (never lock)
            cursor_x += joystick_x * cursor_speed
            cursor_y += joystick_y * cursor_speed
            cursor_x = max(0.0, min(1.0, cursor_x))
            cursor_y = max(0.0, min(1.0, cursor_y))

            cx = int(cursor_x * w)
            cy = int((1.0 - cursor_y) * h)

            center_x, center_y = w // 2, h // 2
            now = time.time()
            fix_radius_px = int(target_radius_ratio * min(w, h))  # use global radius for fixation cross

            if state == "fixation":
                # hold on cross for fixation_hold --> then immediately show target and enter DELAY
                dist = math.hypot(cx - center_x, cy - center_y)
                if dist < fix_radius_px:
                    if fixation_hold_start is None:
                        fixation_hold_start = now
                    elif now - fixation_hold_start >= fixation_hold:
                        # choose target & set visuals
                        target_index = random.randint(0, num_targets - 1)
                        target_pos, target_radius_px, target_color_cur, acceptance = get_target_position(target_index, w, h)
                        cursor_hold_start = None
                        delay_hold_start = None
                        trial_start_time = None  # will start when we enter ACTIVE
                        state = "delay"  # target shown; cross still visible
                else:
                    fixation_hold_start = None

            elif state == "delay":
                # target is visible; cross is visible
                # Keep position synced (handles window resize)
                target_pos, target_radius_px, target_color_cur, _ = get_target_position(target_index, w, h)
                # must KEEP holding the cross for delay_hold; leaving early = FAIL
                dist_center = math.hypot(cx - center_x, cy - center_y)
                if dist_center < fix_radius_px:
                    if delay_hold_start is None:
                        delay_hold_start = now
                    elif now - delay_hold_start >= delay_hold:
                        # Delay satisfied → enter ACTIVE: cross disappears, autofail starts
                        state = "active"
                        trial_start_time = now
                else:
                    # left cross too early --> FAIL immediately
                    pending_result = False
                    iti_end_time = now + intertrial_interval + fail_interval
                    state = "iti"
                    # wipe timers and visuals needed only for active/delay
                    cursor_hold_start = None

            elif state == "active":
                # cross is hidden; target visible; movement allowed
                # Keep position synced
                target_pos, target_radius_px, target_color_cur, acceptance = get_target_position(target_index, w, h)
                # Auto-fail if too slow to acquire
                if trial_start_time and (now - trial_start_time) >= autofail:
                    pending_result = False
                    iti_end_time = now + intertrial_interval + fail_interval
                    state = "iti"
                else:
                    # success detection with per-target hold time (when using custom layout)
                    hold_needed = (
                        float(custom_targets[target_index].get("hold_time", hold_time))
                        if (not use_default_layout and 0 <= target_index < len(custom_targets))
                        else hold_time
                    )

                    dist_tgt = math.hypot(cx - target_pos[0], cy - target_pos[1])
                    if dist_tgt < target_radius_px * acceptance:
                        if cursor_hold_start is None:
                            cursor_hold_start = now
                        elif now - cursor_hold_start >= hold_needed:
                            # SUCCESS --> immediately hide target and enter ITI
                            pending_result = True
                            iti_end_time = now + intertrial_interval + success_interval
                            state = "iti"
                    else:
                        cursor_hold_start = None

            elif state == "iti":
                # target hidden; cross hidden; keep cursor responsive
                if now >= iti_end_time:
                    # Done with ITI --> end of trial
                    return TaskResult(success=bool(pending_result))
                # else keep waiting without blocking

            # refresh
            context.widget.update()
            await asyncio.sleep(0.01)

    if not READING_JOYSTICK:
        READING_JOYSTICK = True
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
            await context.sleep(datetime.timedelta(milliseconds=200))
            ser.reset_input_buffer()
            print(f"[RUN] Opened serial port {ser.port} @ {BAUD_RATE} (timeout=0.1).")
            create_task_with_exc_handling(poll_joystick_from_serial())
        except Exception as e:
            print(f"[RUN] ERROR: Could not open {SERIAL_PORT!r}: {e}")
            raise

    return await trial_loop()

    while True:
        await context.sleep(datetime.timedelta(seconds=1))

    return TaskResult(success=True)
