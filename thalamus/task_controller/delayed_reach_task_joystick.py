"""
Implementation of the simple task, joystick‐driven circle version

> python -m thalamus.task_controller --pypipeline

"""
import typing
import serial
import time
import re
import logging
import datetime
import asyncio

from ..qt import *
from . import task_context
from .. import thalamus_pb2
from .widgets import Form, ListAsTabsWidget
from .util import create_task_with_exc_handling, TaskResult, TaskContextProtocol, CanvasPainterProtocol
from ..config import *

LOGGER = logging.getLogger(__name__)

# ───────── JOYSTICK PARAMS ─────────
SERIAL_PORT = '/dev/cu.usbmodem31101'
BAUD_RATE    = 115200
DEAD_ZONE    = 10
MID          = 512.0

pattern = re.compile(r"x\s*=\s*(\d+)\s*,\s*y\s*=\s*(\d+)")

def normalize(value: int) -> float:
  if abs(value - MID) < DEAD_ZONE:
      value = MID
  if value < 0:
      value = 0
  elif value > 1023:
      value = 1023
  return (value - MID) / MID

# Config = typing.NamedTuple('Config', [
#   ('intertrial_timeout', datetime.timedelta),
#   ('start_timeout',       datetime.timedelta),
#   ('hold_timeout',        datetime.timedelta),
#   ('blink_timeout',       datetime.timedelta),
#   ('fail_timeout',        datetime.timedelta),
#   ('success_timeout',     datetime.timedelta),
#   ('target_rectangle',    QRect),
#   ('target_color',        QColor),
# ])

RANDOM_DEFAULT = {'min': 1, 'max': 1}
COLOR_DEFAULT   = [255, 255, 255]

# class TargetWidget(QWidget):
#   """
#   Widget for managing a target config (unused in the joystick‐only version,
#   but left here in case you still want to configure parameters).
#   """
#   def __init__(self, config: ObservableCollection) -> None:
#     super().__init__()
#     if 'name' not in config:
#       config['name'] = 'Untitled'

#     layout = QGridLayout()
#     self.setLayout(layout)

#     layout.addWidget(QLabel('Name:'), 0, 0)

#     name_edit = QLineEdit(config['name'])
#     name_edit.setObjectName('name_edit')
#     name_edit.textChanged.connect(lambda v: config.update({'name': v}))
#     layout.addWidget(name_edit, 0, 1)

#     def do_copy() -> None:
#       if config.parent:
#         config.parent.append(config.copy())

#     copy_button = QPushButton('Copy Target')
#     copy_button.setObjectName('copy_button')
#     copy_button.clicked.connect(do_copy)
#     layout.addWidget(copy_button, 0, 2)

#     fixed_form = Form.build(config, ['Name:', 'Value:'],
#       Form.Constant('Width', 'width', 10, '\u00B0'),
#       Form.Constant('Height', 'height', 10, '\u00B0'),
#       Form.Constant('Orientation', 'orientation', 0, '\u00B0'),
#       Form.Constant('Window Size', 'window_size', 0, '\u00B0'),
#       Form.Constant('Reward Channel', 'reward_channel', 0),
#       Form.Constant('Audio Scale Left', 'audio_scale_left', 0),
#       Form.Constant('Audio Scale Right', 'audio_scale_right', 0),
#       Form.Color('Color', 'color', QColor(255, 255,255)),
#       Form.Bool('Is Fixation', 'is_fixation', False),
#       Form.Choice('Shape', 'shape', [('Box', 'box'), ('Ellipsoid', 'ellipsoid')]),
#       Form.File('Stl File (Overrides shape)', 'stl_file', '', 'Select Stl File', '*.stl'),
#       Form.File('Audio File', 'audio_file', '', 'Select Audio File', '*.wav'),
#       Form.Bool('Only Play If Channel Is High', 'audio_only_if_high'),
#       Form.Bool('Play In Ear', 'play_in_ear')
#     )
#     layout.addWidget(fixed_form, 1, 1, 1, 2)

#     random_form = Form.build(config, ['Name:', 'Min:', 'Max:'],
#       Form.Uniform('Radius', 'radius', 0, 5, '\u00B0'),
#       Form.Uniform('Angle', 'angle', 0, 360, '\u00B0'),
#       Form.Uniform('Audio Volume', 'volume', 0, 0),
#       Form.Uniform('Auditory Temporal Jitter', 'auditory_temporal_jitter', 0, 0),
#       Form.Uniform('Auditory Spatial Offset', 'auditory_spatial_offset', 0, 0),
#       Form.Uniform('Auditory Spatial Offset Around Fixation', 'auditory_spatial_offset_around_fixation', 0, 0),
#       Form.Uniform('On Luminance', 'on_luminance', 0, 0),
#       Form.Uniform('Off Luminance', 'off_luminance', 0, 0)
#     )
#     layout.addWidget(random_form, 1, 3, 1, 2)


def create_widget(task_config: ObservableCollection) -> QWidget:
  """
  Creates a widget for configuring the simple task (parameters only).
  """
  result = QWidget()
  layout = QVBoxLayout()
  result.setLayout(layout)

  form = Form.build(task_config, ["Name:", "Min:", "Max:"],
    Form.Uniform('Intertrial Interval', 'intertrial_timeout', 1, 1, 's'),
    Form.Uniform('Start Interval',      'start_timeout',       1, 1, 's'),
    Form.Uniform('Hold Interval',       'hold_timeout',        1, 1, 's'),
    Form.Uniform('Blink Interval',      'blink_timeout',       1, 1, 's'),
    Form.Uniform('Fail Interval',       'fail_timeout',        1, 1, 's'),
    Form.Uniform('Success Interval',    'success_timeout',     1, 1, 's'),
    Form.Uniform('Target X',            'target_x',            1, 1, 'px'),
    Form.Uniform('Target Y',            'target_y',            1, 1, 'px'),
    Form.Uniform('Target Width',        'target_width',        1, 1, 'px'),
    Form.Uniform('Target Height',       'target_height',       1, 1, 'px'),
    Form.Color('Color',                 'target_color',        QColor(255, 255, 255))
  )
  layout.addWidget(form)

  new_target_button = QPushButton('Add Target')
  new_target_button.setObjectName('new_target_button')
  new_target_button.clicked.connect(lambda: task_config['targets'].append({}) and None)
  layout.addWidget(new_target_button)

  # if 'targets' not in task_config:
  #   task_config['targets'] = []
  # target_config_list = task_config['targets']
  # target_tabs = ListAsTabsWidget(target_config_list, TargetWidget, lambda t: str(t['name']))
  # layout.addWidget(target_tabs)

  return result

async def run(context: TaskContextProtocol) -> TaskResult:  # pylint: disable=too-many-statements
  """
  Implementation of the task: we simply draw a circle whose position follows an Arduino 
  joystick (read via serial).

  """

  # Set up config and widget assertion ─────────────────────────────────────
  assert context.widget, 'Widget is None; cannot render.'

  # Define an async polling coroutine that reads serial continuously ───────
  async def poll_joystick_from_serial():
    nonlocal joystick_x, joystick_y

    while True:
      raw_bytes = ser.readline()  # byte string, possibly b'' if timed out
      if raw_bytes:
        raw_line = raw_bytes.decode("utf-8", errors="ignore").strip()
        # print(f"\n[DEBUG] raw_line: {raw_line!r}")

        m = pattern.search(raw_line)
        if m:
          raw_x = int(m.group(1))
          raw_y = int(m.group(2))
          # print(f"[DEBUG] parsed raw_x={raw_x}, raw_y={raw_y}")

          x_norm = normalize(raw_x)
          y_norm = normalize(raw_y)
          # print(f"[DEBUG] normalized x={x_norm:+.3f}, y={y_norm:+.3f}")

          joystick_x = x_norm
          joystick_y = y_norm
          context.widget.update()
          
          # try:
          #   response = thalamus_pb2.AnalogResponse()
          #   response.data.extend([x_norm, y_norm])  # 2 channels: X and Y
          #   response.spans.append(thalamus_pb2.Span(begin=0, end=1,name="X"))
          #   response.spans.append(thalamus_pb2.Span(begin=1, end=2,name="Y"))
          #   yield response
          # except Exception as e:
          #     print(f"[WARN] Failed to read or send joystick data: {e}")
          #     continue
            
      else:
        print(f"[DEBUG] no regex match for: {raw_line!r}")
      await asyncio.sleep(0) 

  try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1) # will block up to 100 ms for each line.
    await context.sleep(datetime.timedelta(milliseconds=200))
    ser.reset_input_buffer()
    print(f"[RUN] Opened serial port {ser.port} @ {BAUD_RATE} (timeout=0.1).")
    create_task_with_exc_handling(poll_joystick_from_serial())

  except Exception as e:
    print(f"[RUN] ERROR: Could not open {SERIAL_PORT!r}: {e}")
    raise
  
  # Shared joystick‐state variables ─────────────────────────────────────────
  joystick_x = 0.0
  joystick_y = 0.0  

  # Define a renderer that draws a filled circle at (joystick_x, joystick_y) ──
  def renderer(painter: CanvasPainterProtocol) -> None:
    w = context.widget.width()
    h = context.widget.height()
    cx = int((joystick_x + 1.0) * 0.5 * w)
    cy = int((1.0 - (joystick_y + 1.0) * 0.5) * h)
    radius = 20

    painter.setBrush(QColor(255, 0, 0)) # color brush
    painter.drawEllipse(cx - radius, cy - radius, radius * 2, radius * 2) # ellipse

  context.widget.renderer = renderer

  # Keep run() alive forever ───────────────────────────────────────────
  while True:
    await context.sleep(datetime.timedelta(seconds=1))

  #pylint: disable=unreachable
  # If you ever wanted to end the task, you could break and return TaskResult(True/False).


  """
  Note: Because we’ve launched `poll_joystick_from_serial()` as a background async task,
  it will keep reading the Arduino data, updating joystick_x/joystick_y, and calling
  widget.update(). Meanwhile, `run(...)` itself simply “sleeps” forever so the task never exits.
  """

