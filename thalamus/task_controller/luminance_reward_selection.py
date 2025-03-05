"""
Implementation of the simple task
"""
import time
import enum
import typing
import logging
import datetime
import numpy as np

from ..qt import *

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import (
  wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol, nullcontext, stimulator,
  assert_behav_result_has, RenderOutput
)
from .. import task_controller_pb2
from .. import thalamus_pb2
from ..config import ObservableCollection

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
      Form.Color('Color', 'color', QColor(255, 255,255)),
      Form.Bool('Is Fixation', 'is_fixation', False)
    )
    layout.addWidget(fixed_form, 1, 1, 1, 2)

    random_form = Form.build(config, ['Name:', 'Min:', 'Max:'],
      Form.Uniform('Radius', 'radius', 0, 5, '\u00B0'),
      Form.Uniform('Angle', 'angle', 0, 360, '\u00B0'),
      Form.Uniform('Luminance', 'luminance', 0, 0)
    )
    layout.addWidget(random_form, 1, 3, 1, 2)

class State(enum.Enum):
  FAIL = enum.auto()
  SUCCESS = enum.auto()
  INTERTRIAL = enum.auto()
  START_ON = enum.auto()
  START_ACQ = enum.auto()
  GO = enum.auto()
  TARGS_ACQ = enum.auto()

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
    Form.Uniform('Acquire Interval', 'acquire_timeout', 1, 1, 's'),
    Form.Uniform('Hold 2 Interval', 'hold_2_timeout', 1, 1, 's'),
    Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Constant('Mean Luminance', 'mean_luminance', .5, ''),
    Form.Constant('Min Angle Between Targets', 'min_angle', 90, '\u00B0'),
    Form.Constant('State Indicator X', 'state_indicator_x', 180),
    Form.Constant('State Indicator Y', 'state_indicator_y', 0),
    Form.Choice('Stim Phase', 'stim_phase', [(e.name, e.name) for e in State]),
    #Stimulation parameters
    Form.Constant('Stim Start', 'stim_start', 1, 's'),
    Form.Constant('Intan Config', 'intan_cfg', 2),
    Form.Constant('Pulse Count', 'pulse_count', 1),
    Form.Constant('Pulse Frequency', 'pulse_frequency', 1, 'hz'),
    Form.Constant('Pulse Width', 'pulse_width', 0, 'ms'),
    Form.File('High Audio File', 'high_audio_file', '', 'Select Audio File', '*.wav'),
    Form.File('Low Audio File', 'low_audio_file', '', 'Select Audio File', '*.wav'),
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
  
def pol_to_cart2d(radius, angle):
  xcart = radius*np.cos(np.radians(angle))
  ycart = radius*np.sin(np.radians(angle))
  return xcart, ycart

def ecc_to_px(ecc, dpi):
  """
  converts degrees of eccentricity to pixels relative to the optical center.
  """
  d_m = 0.4 # meters (approximate, TODO: get proper measurement)
  x_m = d_m*np.tan(np.radians(ecc))
  x_inch = x_m/0.0254
  x_px = x_inch*dpi
  return x_px

def get_target_rectangle(context, itarg, dpi):
  canvas = context.widget
  x_ecc, y_ecc = pol_to_cart2d(
    context.get_target_value(itarg, 'radius'),
    context.get_target_value(itarg, 'angle'))

  targ_width_px = ecc_to_px(context.get_target_value(itarg, 'width'), dpi)
  targ_height_px = ecc_to_px(context.get_target_value(itarg, 'height'), dpi)

  ecc = np.array([x_ecc, y_ecc])

  # manually converting this offset to pixel coordinates
  pos_vis = ecc_to_px(ecc, dpi)
  t = np.array([canvas.frameGeometry().width()/2,
        canvas.frameGeometry().height()/2])
  Rvec = np.array([1.0, -1.0]) # manually specifying y axis flip

  p_win = Rvec*pos_vis + t


  return QRect(int(p_win[0] - targ_width_px/2), int(p_win[1] - targ_height_px/2), int(targ_width_px), int(targ_height_px))

def get_target_rectangles(context, dpi):  

  ntargets = len(context.task_config['targets']) # all targets, including fixation and targ2s
    
  all_target_rects = []  
  for itarg in range(ntargets): # looping through all targets, including fixation
    all_target_rects.append(get_target_rectangle(context, itarg, dpi))
  return all_target_rects


def make_relative_targ2_rect(context, i_targ2, origin_target_rect, dpi):
  x_ecc, y_ecc = pol_to_cart2d(
    context.get_target_value(i_targ2, 'radius'),
    context.get_target_value(i_targ2, 'angle'))

  targ_width_px = ecc_to_px(context.get_target_value(i_targ2, 'width'), dpi)
  targ_height_px = ecc_to_px(context.get_target_value(i_targ2, 'height'), dpi)

  canvas = context.widget
  ecc = np.array([x_ecc, y_ecc])

  # manually converting this offset to pixel coordinates
  pos_vis = ecc_to_px(ecc, dpi)
  t = np.array([origin_target_rect.center().x(), origin_target_rect.center().y()])
  Rvec = np.array([1.0, -1.0]) # manually specifying y axis flip

  p_win = Rvec*pos_vis + t

  return QRect(int(p_win[0] - targ_width_px/2), int(p_win[1] - targ_height_px/2), int(targ_width_px), int(targ_height_px))


def get_start_target_index(context):
  all_target_configs = context.task_config['targets']
  i_start_targ = \
    [itarg for itarg, x in enumerate(all_target_configs) if x['is_fixation']]
  if len(i_start_targ) == 0:
    i_start_targ = [0]
  else:
    i_start_targ = i_start_targ[0]

  return i_start_targ

def distance(lhs, rhs):
  return ((lhs.x() - rhs.x())**2 + (lhs.y() - rhs.y())**2)**.5

def toggle_brightness(brightness):
  return 0 if brightness == 255 else 255

async def next_state(context, new_state, stim_phase, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
  await context.log(f'BehavState={new_state.name}')
  return stimulator(context, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period) if new_state == stim_phase else nullcontext()

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

  intertrial_timeout = datetime.timedelta(seconds=context.get_value('intertrial_timeout'))
  start_timeout = datetime.timedelta(seconds=context.get_value('start_timeout'))
  hold_timeout = datetime.timedelta(seconds=context.get_value('hold_timeout'))
  hold_2_timeout = datetime.timedelta(seconds=context.get_value('hold_2_timeout'))
  blink_timeout = datetime.timedelta(seconds=context.get_value('blink_timeout'))
  fail_timeout = datetime.timedelta(seconds=context.get_value('fail_timeout'))
  success_timeout = datetime.timedelta(seconds=context.get_value('success_timeout'))
  acquire_timeout = datetime.timedelta(seconds=context.get_value('acquire_timeout'))

  custom_display_state_x = int(context.task_config['state_indicator_x'])
  custom_display_state_y = int(context.task_config['state_indicator_y'])
  
  high_sound, low_sound = None, None
  if context.task_config['high_audio_file']:
    high_sound = QSound(context.task_config['high_audio_file'])
  if context.task_config['low_audio_file']:
    low_sound = QSound(context.task_config['low_audio_file'])

  #Read stimulation parameters
  try:
    stim_phase = State[context.task_config['stim_phase']]
  except KeyError:
    stim_phase = State.FAIL
  stim_start = datetime.timedelta(seconds=context.task_config['stim_start'])
  intan_cfg = context.task_config['intan_cfg']
  pulse_count = int(context.task_config['pulse_count'])
  pulse_frequency = context.task_config['pulse_frequency']
  pulse_width = datetime.timedelta(milliseconds=context.task_config['pulse_width'])
  pulse_period = datetime.timedelta(seconds=1/pulse_frequency) #interpulse period

  mean_luminance = context.get_value('mean_luminance')
  min_angle = context.get_value('min_angle')
  ntargets = len(context.task_config['targets'])
  i_start_targ = get_start_target_index(context)
  i_periph_targs = [x for x in range(ntargets) if x != i_start_targ]
  n_periph_targs = len(i_periph_targs)

  dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()

  building_rects = True
  center = QPoint(context.widget.width()//2, context.widget.height()//2)
  start = time.perf_counter()
  while building_rects:
    if time.perf_counter() - start > 5:
      QMessageBox.warning(None, 'Invalid Angles', 'Peripheral target angles can\'t satisfy the min angle constraint.')
      return task_context.TaskResult(False, False)
    all_target_rects = get_target_rectangles(context, dpi)
    one = QPointF(all_target_rects[i_periph_targs[0]].center() - center)
    two = QPointF(all_target_rects[i_periph_targs[1]].center() - center)
    one /= np.sqrt(one.x()**2 + one.y()**2)
    two /= np.sqrt(two.x()**2 + two.y()**2)
    building_rects = QPointF.dotProduct(one, two) > np.cos(min_angle*np.pi/180)

  all_target_windows = [ecc_to_px(context.get_target_value(itarg, 'window_size'), dpi)
                        for itarg in range(ntargets)]
  all_target_colors = [context.get_target_color(itarg, 'color', COLOR_DEFAULT) for itarg in range(ntargets)]
  all_target_luminance = [context.get_target_value(i, 'luminance', COLOR_DEFAULT) for i in range(ntargets)]
  all_target_names = [context.get_target_value(i, 'name', None) for i in range(ntargets)]
  all_reward_channels = [context.get_target_value(i, 'reward_channel', None) for i in range(ntargets)]
  all_target_acquired = [False]*ntargets
  all_targets_visible = [False]*ntargets

  first_periph_targ_luminance = all_target_luminance[i_periph_targs[0]]
  all_target_luminance[i_periph_targs[1]] = 2*mean_luminance - first_periph_targ_luminance
  all_target_luminance[i_periph_targs[1]] = max(all_target_luminance[i_periph_targs[1]],
                                                context.task_config['targets'][i_periph_targs[1]]['luminance']['min'])
  context.trial_summary_data.used_values['targ' + str(i_periph_targs[1]) + '_luminance'] = all_target_luminance[i_periph_targs[1]]


  for i in i_periph_targs:
    color = all_target_colors[i]
    luminance = all_target_luminance[i]
    new_color = QColor(int(color.red()*luminance), int(color.green()*luminance), int(color.blue()*luminance))
    all_target_colors[i] = new_color

  """
  Defining drawing and cursor behavior.
  """

  def gaze_handler(cursor: QPoint) -> None:
    nonlocal all_target_acquired
    all_target_acquired = [distance(p[0].center(), cursor) < p[1] for p in zip(all_target_rects, all_target_windows)]

  touched = False
  def touch_handler(cursor: QPoint):
    nonlocal touched
    if cursor.x() < 0:
      return
    touched = True

  context.widget.touch_listener = touch_handler
  context.widget.gaze_listener = gaze_handler
  
  
  state_brightness = 0
  on_time_ms = 0

  show_target = False
  def renderer(painter: CanvasPainterProtocol) -> None:
    nonlocal on_time_ms
    for i, visible in enumerate(all_targets_visible):
      if visible:
        painter.fillRect(all_target_rects[i], all_target_colors[i])
        #painter.fillEllipse(all_target_rects[i].center(), all_target_windows[i])
        
    state_color = QColor(state_brightness, state_brightness, state_brightness)
    state_width = 70
    painter.fillRect(custom_display_state_x, custom_display_state_y, state_width, state_width, state_color)

    with painter.masked(RenderOutput.OPERATOR):
      path = QPainterPath()

      for rect, window in zip(all_target_rects, all_target_windows):
        path.addEllipse(QPointF(rect.center()), window, window)

      painter.fillPath(path, QColor(255, 255, 255, 128))

      painter.setPen(QColor(255, 255, 255))
      font = painter.font()
      font.setPointSize(5*font.pointSize())
      painter.setFont(font)
      painter.drawText(0, 100, str(on_time_ms))

  context.widget.renderer = renderer

  async def fail():
    nonlocal state_brightness, all_targets_visible
    with await next_state(context, State.FAIL, stim_phase, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
      all_targets_visible = [False]*ntargets
      state_brightness = toggle_brightness(state_brightness)
      context.widget.update()
      await context.sleep(fail_timeout)
      return task_context.TaskResult(False)

  while True:
    with await next_state(context, State.INTERTRIAL, stim_phase, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
      all_targets_visible[i_start_targ] = False
      state_brightness = toggle_brightness(state_brightness)
      context.widget.update()
      await wait_for(context, lambda: touched, intertrial_timeout)
      if touched:
        return await fail()

    with await next_state(context, State.START_ON, stim_phase, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
      all_targets_visible[i_start_targ] = True
      state_brightness = toggle_brightness(state_brightness)
      context.widget.update()
      acquired = await wait_for(context, lambda: all_target_acquired[i_start_targ] or touched, start_timeout)
      if touched:
        return await fail()

    if acquired:
      break

  # state: startacq
  with await next_state(context, State.START_ACQ, stim_phase, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
    success = await context.any(wait_for_hold(context, lambda: all_target_acquired[i_start_targ], hold_timeout, blink_timeout), context.until(lambda: touched))
  if not success or touched:
    return await fail()

  all_targets_visible[i_start_targ] = False
  for i in i_periph_targs:
    all_targets_visible[i] = True
  state_brightness = toggle_brightness(state_brightness)
  context.widget.update()

  with await next_state(context, State.GO, stim_phase, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
    success = await wait_for(context, lambda: any(all_target_acquired[i] for i in i_periph_targs) or touched, acquire_timeout)
  if not success or touched:
    return await fail()

  acquired_targ = [p[0] for p in enumerate(all_target_acquired) if p[1]][0]
  with await next_state(context, State.TARGS_ACQ, stim_phase, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
    success = await context.any(wait_for_hold(context, lambda: all_target_acquired[acquired_targ], hold_2_timeout, blink_timeout), context.until(lambda: touched))
  if not success or touched:
    return await fail()

  reward_given = context.get_reward(all_reward_channels[acquired_targ])
  on_time_ms = int(reward_given)
  signal = thalamus_pb2.AnalogResponse(
      data=[5,0],
      spans=[thalamus_pb2.Span(begin=0,end=2,name='Reward')],
      sample_intervals=[1_000_000*on_time_ms])

  rewards = [context.get_reward(c) for c in all_reward_channels]
  reward_is_high = True
  for i, reward in enumerate(rewards):
    if i != acquired_targ and reward > reward_given:
      if low_sound is not None:
        low_sound.play()
      reward_is_high = False
  if reward_is_high and high_sound is not None:
    high_sound.play()
  #access the delivered reward again so task_context sets self.trial_summary_data.used_values
  reward_given = context.get_reward(all_reward_channels[acquired_targ])

  await context.inject_analog('Reward', signal)

  print("delivering reward %d"%(on_time_ms,) )
  context.behav_result = {
    'chosen_target': acquired_targ,
    'reward_given': reward_given,
    'rewards': [r for i, r in enumerate(rewards) if i != i_start_targ]
  }

  all_targets_visible = [False]*ntargets
  state_brightness = toggle_brightness(state_brightness)
  context.widget.update()
  with await next_state(context, State.SUCCESS, stim_phase, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
    await context.sleep(success_timeout if reward_is_high else fail_timeout)
  return task_context.TaskResult(True)

