"""
Implementation of the delayed center-out reach task, joystick‐driven circle version

> python -m thalamus.task_controller --pypipeline

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
SERIAL_PORT = '/dev/cu.usbmodem31101'
BAUD_RATE = 115200
DEAD_ZONE = 10
MID = 512.0

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
    layout = QVBoxLayout()
    result.setLayout(layout)

    form = Form.build(task_config, ["Parameter", "Value"],
        Form.Constant("Hold Time (s)", "hold_time", 1.0, "s"),
        Form.Constant("Fixation Hold Time (s)", "fixation_hold", 0.5, "s"),
        Form.Constant("Target Radius (% of min screen dim)", "target_radius_ratio", 0.05),
        Form.Constant("Radial Distance (% of min screen dim)", "target_distance_ratio", 0.3),
        Form.Color("Target Color", "target_color", QColor(0, 255, 0)),
        Form.Constant("Cursor Diameter (% of min screen dim)", "cursor_diameter_ratio", 0.03),
        Form.Color("Cursor Color", "cursor_color", QColor(255, 0, 0))
    )
    layout.addWidget(form)
    return result

async def run(context: TaskContextProtocol) -> TaskResult:
    assert context.widget, 'Widget is None; cannot render.'

    task_config = context.config["queue"][0]
    hold_time = task_config.get("hold_time", 1.0)
    fixation_hold = task_config.get("fixation_hold", 0.5)
    target_radius_ratio = task_config.get("target_radius_ratio", 0.05)
    target_distance_ratio = task_config.get("target_distance_ratio", 0.3)
    target_color = QColor(*task_config.get("target_color", [0, 255, 0]))
    cursor_diameter_ratio = task_config.get("cursor_diameter_ratio", 0.03)
    cursor_color = QColor(*task_config.get("cursor_color", [255, 0, 0]))

    joystick_x = 0.0
    joystick_y = 0.0

    state = "fixation"
    state_timer = time.time()

    target_index = 0
    target_pos = (0, 0)
    cursor_hold_start = None
    fixation_hold_start = None
    num_targets = 8

    def get_target_position(index: int, width: int, height: int):
        min_dim = min(width, height)
        target_distance = int(target_distance_ratio * min_dim)
        angle = 2 * math.pi * index / num_targets
        cx, cy = width // 2, height // 2
        tx = cx + target_distance * math.cos(angle)
        ty = cy + target_distance * math.sin(angle)
        return int(tx), int(ty)

    def renderer(painter: CanvasPainterProtocol) -> None:
        w = context.widget.width()
        h = context.widget.height()
        cx = int((joystick_x + 1.0) * 0.5 * w)
        cy = int((1.0 - (joystick_y + 1.0) * 0.5) * h)

        min_dim = min(w, h)
        target_radius_px = int(target_radius_ratio * min_dim)
        cursor_diameter = int(cursor_diameter_ratio * min_dim)

        # Draw fixation cross
        if state == "fixation":
            painter.setPen(QPen(QColor(255, 255, 255), 2))
            center_x, center_y = w // 2, h // 2
            painter.drawLine(center_x - 10, center_y, center_x + 10, center_y)
            painter.drawLine(center_x, center_y - 10, center_x, center_y + 10)

        # Draw target
        if state in ["active", "success_delay"]:
            painter.setPen(QPen(target_color, 1))
            painter.setBrush(target_color)
            painter.drawEllipse(target_pos[0] - target_radius_px, target_pos[1] - target_radius_px, 2 * target_radius_px, 2 * target_radius_px)

        # Draw cursor
        r = cursor_diameter // 2
        painter.setPen(QPen(cursor_color, 1))
        painter.setBrush(cursor_color)
        painter.drawEllipse(cx - r, cy - r, cursor_diameter, cursor_diameter)

    context.widget.renderer = renderer

    async def poll_joystick_from_serial():
        nonlocal joystick_x, joystick_y

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
            await asyncio.sleep(0)

    async def trial_loop():
        nonlocal state, state_timer, target_index, target_pos, cursor_hold_start, fixation_hold_start

        while True:
            w, h = context.widget.width(), context.widget.height()
            cx = int((joystick_x + 1.0) * 0.5 * w)
            cy = int((1.0 - (joystick_y + 1.0) * 0.5) * h)
            min_dim = min(w, h)
            target_radius_px = int(target_radius_ratio * min_dim)
            center_x, center_y = w // 2, h // 2
            now = time.time()

            if state == "fixation":
                dist = math.hypot(cx - center_x, cy - center_y)
                if dist < target_radius_px:
                    if fixation_hold_start is None:
                        fixation_hold_start = now
                    elif now - fixation_hold_start >= fixation_hold:
                        state = "wait"
                        state_timer = now + 1.0
                else:
                    fixation_hold_start = None

            elif state == "wait" and now >= state_timer:
                target_index = random.randint(0, num_targets - 1)
                target_pos = get_target_position(target_index, w, h)
                cursor_hold_start = None
                state = "active"

            elif state == "active":
                dist = math.hypot(cx - target_pos[0], cy - target_pos[1])
                if dist < target_radius_px:
                    if cursor_hold_start is None:
                        cursor_hold_start = now
                    elif now - cursor_hold_start >= hold_time:
                        print(f"[SUCCESS] Trial complete at target {target_index}")
                        state = "success_delay"
                        state_timer = now + 1.0
                else:
                    cursor_hold_start = None

            elif state == "success_delay" and now >= state_timer:
                state = "fixation"
                fixation_hold_start = None

            context.widget.update()
            await asyncio.sleep(0.01)

    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        await context.sleep(datetime.timedelta(milliseconds=200))
        ser.reset_input_buffer()
        print(f"[RUN] Opened serial port {ser.port} @ {BAUD_RATE} (timeout=0.1).")
        create_task_with_exc_handling(poll_joystick_from_serial())
    except Exception as e:
        print(f"[RUN] ERROR: Could not open {SERIAL_PORT!r}: {e}")
        raise

    create_task_with_exc_handling(trial_loop())

    while True:
        await context.sleep(datetime.timedelta(seconds=1))