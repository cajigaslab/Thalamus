"""
Implementation of the simple task
"""
import typing
import logging
import datetime

from ..qt import *

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol, create_task_with_exc_handling
from .. import task_controller_pb2
from ..config import *
from ..thalamus_pb2 import StimDeclaration, AnalogResponse, Span
import numpy 

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
  ('target_color2', QColor) #second color
])

RANDOM_DEFAULT = {'min': 1, 'max':1}
COLOR_DEFAULT = [255, 255, 255]

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
      Form.Choice('Shape', 'shape', [('Box', 'box'), ('Ellipsoid', 'ellipsoid')]),
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
      Form.Uniform('Off Luminance', 'off_luminance', 0, 0)
    )
    layout.addWidget(random_form, 1, 3, 1, 2)

def create_widget(task_config: ObservableCollection) -> QWidget:
  """
  Creates a widget for configuring the simple task
  """

  result = QWidget()
  layout = QVBoxLayout()
  result.setLayout(layout)

  # Force default values (overwrite existing values)
  task_config['target_width'] = {'min': 300, 'max': 300}
  task_config['target_height'] = {'min': 300, 'max': 300}
  task_config['intertrial_timeout'] = {'min': 6, 'max': 6} #how long in between "units" (task and rest)
  task_config['start_timeout'] = {'min': 5, 'max': 5}  # duration of task in s
  task_config['hold_timeout'] = {'min': 2, 'max': 2}  # duration of rest/freetime after task in s
  
  #use LOG node instead to track messages
  '''# Add text input box for messages
  message_layout = QHBoxLayout()
  message_label = QLabel('Notes:')
  message_input = QLineEdit()
  message_input.setPlaceholderText('Type message and press Enter...')
  
  def on_message_entered():
    message = message_input.text()
    if message.strip():  # Only log non-empty messages
      LOGGER.info(f'User Message: {message}') #will this save to continuous data stream??
      print(f'User Message: {message}')  # Also print to console
      create_task_with_exc_handling(context.log(f'User Message: {message}'))
      message_input.clear()
  
  message_input.returnPressed.connect(on_message_entered)
  
  message_layout.addWidget(message_label)
  message_layout.addWidget(message_input)
  layout.addLayout(message_layout)'''


  """
  Below: We're building a Form (widgets.py) object that will use task_config to initialize
  the parameters of this task. Values are taken from the provided "task_config" argument, and
  if the key (e.g. intertrial_timeout) is not found in the task_config, the parameters will
  default to the values provided below. The build function also wires up all the
  listeners to update the task_config when changes are made.
  """
  form = Form.build(task_config, 
    ["Name:", "Min:", "Max:"],
    Form.Uniform('Individual Task Duration', 'start_timeout', 1, 1, 's'), #if delete variables here, delete later references too
    Form.Uniform('Subsequent Rest Duration', 'hold_timeout', 1, 1, 's'),
    #Form.Constant('Stim Frequency', 'frequency', 80, 'Hz'),
    #Form.Constant('Stim Bout Duration', 'duration', .5, 's'),
    Form.Color('Task Box Color', 'target_color', QColor(255, 255, 255)),
    Form.Color('Rest Box Color', 'target_color2', QColor(255, 222, 33)),  # Yellow default
    Form.Uniform('Intertrial Timeout', 'intertrial_timeout', 1, 1, 's'), 
    Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Uniform('Target X', 'target_x', 1, 1, 'px'),
    Form.Uniform('Target Y', 'target_y', 1, 1, 'px'),
    Form.Uniform('Target Width', 'target_width', 1, 1, 'px'),
    Form.Uniform('Target Height', 'target_height', 1, 1, 'px')
  )
  layout.addWidget(form)

  if 'targets' not in task_config:
    task_config['targets'] = []
  target_config_list = task_config['targets']
  target_tabs = ListAsTabsWidget(target_config_list, TargetWidget, lambda t: str(t['name']))
  layout.addWidget(target_tabs)

  return result

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
    context.get_color('target_color2', [255, 222, 33])  # Yellow default
  )

  '''stim_declaration: StimDeclaration = StimDeclaration()
  stim_data: AnalogResponse = stim_declaration.data
  stim_data.channel_type = AnalogResponse.ChannelType.Voltage

  frequency = context.get_value('frequency')
  interval = 1/frequency
  duration = context.get_value('duration')
  repeats = duration // interval
  data = numpy.zeros((2*repeats,))
  data[::2] = 5
  data[1::2] = 0
  stim_data.data.extend(data)
  stim_data.spans.append(Span(begin=0,end=len(stim_data),name='Dev1/ao0'))
  stim_data.sample_intervals(1e9 * interval)  # in nanoseconds
  await context.arm_stim('DAQOUT', stim_declaration)'''

  """
  Defining drawing and cursor behavior.
  """

  task_name = context.task_config.get('name', 'Unnamed Task')

  show_target = False
  current_text = ""
  current_color = config.target_color
  
  def renderer(painter: CanvasPainterProtocol) -> None:
    if show_target:
      painter.fillRect(config.target_rectangle, current_color)
      painter.setPen(QColor(0, 0, 0))
      font = painter.font()
      font.setPointSize(24)
      painter.setFont(font)
      center_x = config.target_rectangle.x() + config.target_rectangle.width() // 2
      center_y = config.target_rectangle.y() + config.target_rectangle.height() // 2
      painter.drawText(center_x - 50, center_y, current_text)

  context.widget.renderer = renderer

  # Display first square with task name
  await context.log('BehavState=square1')
  await context.log(f'{task_name} start')
  #await context.trigger_stim('DAQOUT')
  #await context.log(f'TTL PULSE SENT')
  show_target = True
  current_text = task_name
  current_color = config.target_color #color defined by user
  # Log task name to terminal
  LOGGER.info(f'{task_name} START')
  print(f'Start of: {task_name}')

  #add usb serial communication here to trigger ttl pulse in ni-daq #1

  context.widget.update()
  await context.sleep(config.start_timeout)  # Duration for first square
  await context.log(f'{task_name} end')
  #await context.trigger_stim('DAQOUT')
  #await context.log(f'TTL PULSE SENT')




  # Display second square (you can change text/position/color)
  await context.log('BehavState=square2')
  current_text = "FreeTime"  # Change this to whatever text you want
  current_color = config.target_color2  # Switch to second color
  LOGGER.info(f'{task_name} END')
  print(f'End of: {task_name}')

  #add usb serial communication here to trigger ttl pulse in ni-daq #2

  context.widget.update()
  await context.sleep(config.hold_timeout)  # Duration for second square
  
  return TaskResult(True)

  '''target_acquired = False
  def touch_handler(cursor: QPoint) -> None:
    nonlocal target_acquired
    target_acquired = config.target_rectangle.contains(cursor)

  context.widget.touch_listener = touch_handler

  show_target = False
  def renderer(painter: CanvasPainterProtocol) -> None:
    if show_target:
      painter.fillRect(config.target_rectangle, config.target_color)
      # Draw the task name in the center of the target
      try:
        # Draw the task name in the center of the target
        painter.setPen(QColor(0, 0, 0))
        font = painter.font()
        font.setPointSize(24)  # Set font size (increase this number for larger text)
        painter.setFont(font)
        center_x = config.target_rectangle.x() + config.target_rectangle.width() // 2
        center_y = config.target_rectangle.y() + config.target_rectangle.height() // 2
        painter.drawText(center_x-50, center_y, task_name)
      except Exception as e:
        LOGGER.error(f"Error drawing task name: {e}")
    painter.drawText(10, 10, f'Demo: {demo}')

  context.widget.renderer = renderer

  while True:
    await context.log('BehavState=intertrial')
    show_target = False
    context.widget.update()
    await context.sleep(config.intertrial_timeout)

    await context.log('BehavState=start_on')
    show_target = True
    context.widget.update()
    acquired = await wait_for(context, lambda: target_acquired, config.start_timeout)

    if acquired:
      break

  # state: startacq
  await context.log('BehavState=startacq')
  success = await wait_for_hold(context, lambda: target_acquired, config.hold_timeout, config.blink_timeout)
  '''
  """
  The trial's outcome (success or failure) at this point is decided, and now
  we can wait (optionally) by success_timeout or fail_timeout.
  """
  '''show_target = False
  context.widget.update()
  if success:
    await context.log('BehavState=success')

    await context.sleep(config.success_timeout)
    return TaskResult(True)

  await context.log('BehavState=fail')

  await context.sleep(config.fail_timeout)
  return TaskResult(False)'''
  #pylint: disable=unreachable
  """
  The return value is a TaskResult instance, and this contains the success/failure,
  as well as maybe other things that we want to add.
  """
