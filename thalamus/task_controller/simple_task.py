"""
Implementation of the simple task
"""
import typing
import logging
import datetime
import random
import numpy as np # import Numpy to draw Gaussian

from ..qt import *

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol
from .. import task_controller_pb2
from ..config import *

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
  ('intertrial_timeout', datetime.timedelta),
  ('start_timeout', datetime.timedelta),
  ('hold_timeout', datetime.timedelta),
  ('blink_timeout', datetime.timedelta),
  ('fail_timeout', datetime.timedelta),
  ('success_timeout', datetime.timedelta),
  ('target_rectangle', QRect),
  ('target_color', QColor),
  ('shape', str)  # Add the shape attribute
])

RANDOM_DEFAULT = {'min': 1, 'max':1}
COLOR_DEFAULT = [255, 255, 255]
shapes = ['rectangle', 'gaussian'] # Define the possible shapes

class TargetWidget(QWidget):
  '''
  Widget for managing a target config
  '''
  def __init__(self, config: ObservableCollection) -> None:
    super().__init__()
    if 'name' not in config:
      config['name'] = 'Untitled'

    layout = QGridLayout()
    self.setLayout(layout)

    layout.addWidget(QLabel('Name:'), 0, 0)

    name_edit = QLineEdit(config['name'])
    name_edit.setObjectName('name_edit')
    name_edit.textChanged.connect(lambda v: config.update({'name': v}))
    layout.addWidget(name_edit, 0, 1)

    def do_copy() -> None:
      if config.parent:
        config.parent.append(config.copy())

    copy_button = QPushButton('Copy Target')
    copy_button.setObjectName('copy_button')
    copy_button.clicked.connect(do_copy)
    layout.addWidget(copy_button, 0, 2)

    fixed_form = Form.build(config, ['Name:', 'Value:'],
      Form.Constant('Width', 'width', 10, '\u00B0'),
      Form.Constant('Height', 'height', 10, '\u00B0'),
      Form.Constant('Orientation', 'orientation', 0, '\u00B0'),
      Form.Constant('Window Size', 'window_size', 0, '\u00B0'),
      Form.Constant('Reward Channel', 'reward_channel', 0),
      Form.Constant('Audio Scale Left', 'audio_scale_left', 0),
      Form.Constant('Audio Scale Right', 'audio_scale_right', 0),
      Form.Color('Color', 'color', QColor(255, 255,255)),
      Form.Bool('Is Fixation', 'is_fixation', False),
      Form.Choice('Shape', 'shape', shapes),  # Add the shape attribute
      Form.File('Stl File (Overrides shape)', 'stl_file', '', 'Select Stl File', '*.stl'),
      Form.File('Audio File', 'audio_file', '', 'Select Audio File', '*.wav'),
      Form.Bool('Only Play If Channel Is High', 'audio_only_if_high'),
      Form.Bool('Play In Ear', 'play_in_ear')
    )
    layout.addWidget(fixed_form, 1, 1, 1, 2)

    random_form = Form.build(config, ['Name:', 'Min:', 'Max:'],
      Form.Uniform('Radius', 'radius', 0, 5, '\u00B0'),
      Form.Uniform('Angle', 'angle', 0, 360, '\u00B0'),
      Form.Uniform('Audio Volume', 'volume', 0, 0),
      Form.Uniform('Auditory Temporal Jitter', 'auditory_temporal_jitter', 0, 0),
      Form.Uniform('Auditory Spatial Offset', 'auditory_spatial_offset', 0, 0),
      Form.Uniform('Auditory Spatial Offset Around Fixation', 'auditory_spatial_offset_around_fixation', 0, 0),
      Form.Uniform('On Luminance', 'on_luminance', 0, 0),
      Form.Uniform('Off Luminance', 'off_luminance', 0, 0),
      Form.Constant('Shape', 'shape', random.choice(shapes))  # Randomly select the shape
    )
    layout.addWidget(random_form, 1, 3, 1, 2)

def create_widget(task_config: ObservableCollection) -> QWidget:
  """
  Creates a widget for configuring the simple task
  """
  result = QWidget()
  layout = QVBoxLayout()
  result.setLayout(layout)

  """
  Below: We're building a Form (widgets.py) object that will use task_config to initialize
  the parameters of this task. Values are taken from the provided "task_config" argument, and
  if the key (e.g. intertrial_timeout) is not found in the task_config, the parameters will
  default to the values provided below. The build function also wires up all the
  listeners to update the task_config when changes are made.
  """
  form = Form.build(task_config, ["Name:", "Min:", "Max:"],
    Form.Uniform('Intertrial Interval', 'intertrial_timeout', 1, 1, 's'),
    Form.Uniform('Start Interval', 'start_timeout', 1, 1, 's'),
    Form.Uniform('Hold Interval', 'hold_timeout', 1, 1, 's'),
    Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Uniform('Target X', 'target_x', 300, 300, 'px'),
    Form.Uniform('Target Y', 'target_y', 300, 300, 'px'),
    Form.Uniform('Target Width', 'target_width', 333, 333, 'px'),
    Form.Uniform('Target Height', 'target_height', 333, 333, 'px'),
    Form.Color('Color', 'target_color', QColor(255, 255, 255)),
    Form.Constant('Shape', 'shape',  random.choice(shapes))  # Add the shape attribute
  )
  layout.addWidget(form)

  new_target_button = QPushButton('Add Target')
  new_target_button.setObjectName('new_target_button')
  new_target_button.clicked.connect(lambda: task_config['targets'].append({}) and None)
  layout.addWidget(new_target_button)

  if 'targets' not in task_config:
    task_config['targets'] = []
  target_config_list = task_config['targets']
  target_tabs = ListAsTabsWidget(target_config_list, TargetWidget, lambda t: str(t['name']))
  layout.addWidget(target_tabs)

  return result

def stamp_msg(context: TaskContextProtocol, msg: 'BehavState') -> 'BehavState':
  '''
  Set the timestamp field to the current time
  '''
  msg.header.stamp = context.ros_now().to_msg()
  return msg

async def run(context: TaskContextProtocol) -> TaskResult: #pylint: disable=too-many-statements
  """
  Implementation of the state machine for the simple task
  """

  """
  Below is an object that contains a realization generated by sampling from the random
  distributions defined in the task_config. It itself has no logic, it simply holds
  the realization's values.
  """
  assert context.widget, 'Widget is None'

  config = Config(
    datetime.timedelta(seconds=context.get_value('intertrial_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('start_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('hold_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('blink_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('fail_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('success_timeout', RANDOM_DEFAULT)),
    QRect(int(context.get_value('target_x', RANDOM_DEFAULT)),
                       int(context.get_value('target_y', RANDOM_DEFAULT)),
                       int(context.get_value('target_width', RANDOM_DEFAULT)),
                       int(context.get_value('target_height', RANDOM_DEFAULT))),
    context.get_color('target_color', COLOR_DEFAULT),
    context.get_value('shape', random.choice(shapes))  # Add the shape attribute
  )

  """
  Defining drawing and cursor behavior.
  """

  target_acquired = False
  def touch_handler(cursor: QPoint) -> None:
    nonlocal target_acquired
    target_acquired = config.target_rectangle.contains(cursor)

  context.widget.touch_listener = touch_handler
  
  show_target = False
  
  def renderer(painter: CanvasPainterProtocol) -> None: 
      # Defines a function renderer that takes a painter object implementing the CanvasPainterProtocol.
      if show_target:
          if config.shape == 'gaussian':
              # Draw Gaussian shape
              rect = config.target_rectangle
              width, height = rect.width(), rect.height()
              center_x, center_y = rect.center().x(), rect.center().y()
              sigma_x, sigma_y = width / 6, height / 6  # Calculates the standard deviations for the Gaussian distribution.
  
              for x in range(rect.left(), rect.right()):
                  for y in range(rect.top(), rect.bottom()):
                      dx, dy = x - center_x, y - center_y # Calculates the distance from the center for each pixel.
                      value = np.exp(-(dx**2 / (2 * sigma_x**2) + dy**2 / (2 * sigma_y**2))) # Computes the Gaussian value for each pixel.
                      color = QColor(config.target_color)
                      color.setAlphaF(value) # Sets the color with an alpha value based on the Gaussian value.
                      painter.setPen(color)
                      painter.drawPoint(x, y) # Draws the point at the specified coordinates.
          else:
              # Draw rectangular shape (i.e. fills the rectangle with the target color)
              painter.fillRect(config.target_rectangle, config.target_color)
  
  context.widget.renderer = renderer # Assigns the renderer function to the widget's renderer attribute.

  context.widget.renderer = renderer

  while True:
    await context.log('BehavState=intertrial')
    show_target = False
    context.widget.update()
    await context.sleep(config.intertrial_timeout) # Sleeps for the intertrial timeout duration.

    await context.log('BehavState=start_on')
    show_target = True
    context.widget.update()
    acquired = await wait_for(context, lambda: target_acquired, config.start_timeout) # Waits for the target to be acquired within the start timeout duration.

    if acquired: # If the target was acquired within the start timeout duration, break out of the loop
      break

  # state: startacq
  success = await wait_for_hold(context, lambda: target_acquired, config.hold_timeout, config.blink_timeout)
  # Waits for the target to be held within the hold timeout and blink timeout durations.

  """
  The trial's outcome (success or failure) at this point is decided, and now
  we can wait (optionally) by success_timeout or fail_timeout.
  """
  show_target = False
  context.widget.update()
  if success:
    await context.log('BehavState=success')

    await context.sleep(config.success_timeout)
    return TaskResult(True) # Returns a TaskResult indicating success.

  await context.log('BehavState=fail')

  await context.sleep(config.fail_timeout)
  return TaskResult(False)
  #pylint: disable=unreachable
  """
  The return value is a TaskResult instance, and this contains the success/failure,
  as well as maybe other things that we want to add.
  """
