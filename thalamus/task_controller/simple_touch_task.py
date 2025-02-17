#pylint: skip-file
#type: ignore
"""
Implementation of the simple task
"""
import time
import math
import typing
import asyncio
import logging
import datetime
import numpy as np
import os

import stl

from ..qt import *

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, RenderOutput, animate
from .. import thalamus_pb2
from .. import task_controller_pb2
from .. import config
from . import task_context

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
  ('intertrial_timeout', datetime.timedelta),
  ('start_timeout', datetime.timedelta),
  ('hold_timeout', datetime.timedelta),
  ('blink_timeout', datetime.timedelta),
  ('fail_timeout', datetime.timedelta),
  ('success_timeout', datetime.timedelta),
  ('is_choice', bool),
])

RANDOM_DEFAULT = {'min': 1, 'max':1}
COLOR_DEFAULT = [255, 255, 255]

class TargetWidget(QWidget):
  '''
  Widget for managing a target config
  '''
  def __init__(self, config: config.ObservableCollection) -> None:
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
      Form.Uniform('On Luminance', 'on_luminance', 1, 1),
      Form.Uniform('Off Luminance', 'off_luminance', 0, 0)
    )
    layout.addWidget(random_form, 1, 3, 1, 2)

def create_widget(task_config: config.ObservableCollection) -> QWidget:
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
    Form.Bool('Is Choice', 'is_choice', False),
    Form.Constant('State Indicator X', 'state_indicator_x', 180),
    Form.Constant('State Indicator Y', 'state_indicator_y', 0),
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

def get_target_rectangles(context, dpi):
  all_target_rects = []

  ntargets = len(context.task_config['targets'])
  for itarg in range(ntargets): # looping through all targets, including fixation

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

    all_target_rects.append(QRect(int(p_win[0] - targ_width_px/2), int(p_win[1] - targ_height_px/2), int(targ_width_px), int(targ_height_px)))

  return all_target_rects

def distance(lhs, rhs):
  return ((lhs.x() - rhs.x())**2 + (lhs.y() - rhs.y())**2)**.5

@animate(30)
async def run(context: task_context.TaskContextProtocol) -> task_context.TaskResult: #pylint: disable=too-many-statements
  """
  Implementation of the state machine for the simple task
  """
  success_sound = QSound(os.path.join(os.path.dirname(__file__), 
      'success_clip.wav'))
  fail_sound = QSound(os.path.join(os.path.dirname(__file__), 
      'failure_clip.wav'))
  show_touch_pos_feedback = False
  """
  Below is an object that contains a realization generated by sampling from the random
  distributions defined in the task_config. It itself has no logic, it simply holds
  the realization's values.
  """
  config = Config(
    datetime.timedelta(seconds=context.get_value('intertrial_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('start_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('hold_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('blink_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('fail_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('success_timeout', RANDOM_DEFAULT)),
    context.get_value('is_choice'),
  )
  
  custom_display_state_x = int(context.task_config['state_indicator_x'])
  custom_display_state_y = int(context.task_config['state_indicator_y'])

  """
  Identifying the "start" target, akin to the fixation target.
  The behavior defined as follows. Any target that is marked "is fixation" is
  the start target. If none of them are defined as the start target, then default to
  the first target in the target list. If multiple are selected, then choose the
  first that is selected.
  """
  ntargets = len(context.task_config['targets'])  
  i_periph_targs = [x for x in range(ntargets)]
  n_periph_targs = len(i_periph_targs)

  dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()

  all_target_rects = get_target_rectangles(context, dpi)
  all_target_windows = [ecc_to_px(context.get_target_value(itarg, 'window_size'), dpi)
                        for itarg in range(ntargets)]
  all_target_colors = [context.get_target_color(itarg, 'color', COLOR_DEFAULT) for itarg in range(ntargets)]
  all_target_on_luminance = [context.get_target_value(i, 'on_luminance', COLOR_DEFAULT) for i in range(ntargets)]
  all_target_off_luminance = [context.get_target_value(i, 'off_luminance', COLOR_DEFAULT) for i in range(ntargets)]
  all_target_names = [context.get_target_value(i, 'name', None) for i in range(ntargets)]
  all_reward_channels = [context.get_target_value(i, 'reward_channel', None) for i in range(ntargets)]
  def load_stl(filename: str) -> typing.Optional[stl.mesh.Mesh]:
    if not filename:
      return None

    return stl.mesh.Mesh.from_file(filename)
  all_target_stls = [load_stl(context.get_target_value(i, 'stl_file')) for i in range(ntargets)]

  """
  Defining drawing and cursor behavior.
  """

  i_periph_targ_to_present = np.random.randint(n_periph_targs)
  i_presented_targ = i_periph_targs[i_periph_targ_to_present]

  blank_space_touched = False
  presented_targ_acquired = False
  i_selected_target = None
  last_selected_target = None
  touch_pos = QPoint()
  def touch_handler(cursor: QPoint) -> None:
    nonlocal blank_space_touched
    nonlocal presented_targ_acquired
    nonlocal i_selected_target
    nonlocal last_selected_target
    nonlocal touch_pos
    in_windows = [distance(all_target_rects[i].center(), cursor) < all_target_windows[i]
                                  for i in range(ntargets)]
    blank_space_touched = blank_space_touched or not any(in_windows) and cursor.x() >= 0

    if config.is_choice:

      in_periph_windows = [distance(all_target_rects[i].center(), cursor) < all_target_windows[i]
                                    for i in range(ntargets)]
      presented_targ_acquired = any(in_periph_windows)

      if presented_targ_acquired:
        i_selected_target = [i for i in range(len(in_windows)) if in_windows[i]][0]
        last_selected_target = i_selected_target
      else:
        i_selected_target = None

    else:
      presented_targ_acquired = distance(
        all_target_rects[i_presented_targ].center(), cursor) < all_target_windows[i_presented_targ]
      if presented_targ_acquired:
        i_selected_target = i_presented_targ
        last_selected_target = i_selected_target
      else:
        i_selected_target = None
    touch_pos = cursor  
  context.widget.touch_listener = touch_handler

  
  show_presented_target = False
  state_brightness = 0

  def renderer(painter: QPainter) -> None:   
    window = all_target_windows[0]

    if config.is_choice:
      if show_presented_target:
        for i, value in enumerate(zip(all_target_rects, all_target_colors, all_target_stls)):
          
          rect, color, stl_mesh = value
          if stl_mesh:
            painter.render_stl(stl_mesh)
          else:
            painter.fillRect(rect, color)
    else:
      if show_presented_target:
        stl_mesh = all_target_stls[i_presented_targ]
        if stl_mesh:
          painter.render_stl(stl_mesh)
        else:
          painter.fillRect(all_target_rects[i_presented_targ], all_target_colors[i_presented_targ])

    with painter.masked(RenderOutput.OPERATOR):
      path = QPainterPath()

      for rect in all_target_rects:
        path.addEllipse(QPointF(rect.center()), window, window)

      painter.fillPath(path, QColor(255, 255, 255, 128))

    #state_color = QColor(state_brightness, state_brightness, state_brightness)
    #painter.fillRect(context.widget.width() - 30, context.widget.height() - 30, 30, 30, state_color)
    state_color = QColor(state_brightness, state_brightness, state_brightness)
    state_width = 40

    custom_display_state_pos = True
    custom_display_state_width = 70

    if custom_display_state_pos:
      painter.fillRect(custom_display_state_x, custom_display_state_y, custom_display_state_width, custom_display_state_width, state_color)
    else:
      painter.fillRect(context.widget.width() - state_width, context.widget.height() - state_width, state_width, state_width, state_color)


    if show_touch_pos_feedback:
      cursor_color = QColor(255, 0, 0)
      cursor_width = 20

      voltage = context.widget.last_voltage
      painter.fillRect(touch_pos.x() - int(cursor_width/2), touch_pos.y() - int(cursor_width/2.0), cursor_width, cursor_width, cursor_color)
      painter.setPen(QColor(255, 0, 0, 255))
      painter.drawText(touch_pos, '   x: %d, y: %d, Vx: %0.2f, Vy: %0.2f'%(touch_pos.x(), touch_pos.y(), voltage.x(), voltage.y()))

  
  context.widget.renderer = renderer

  behav_result = {}
  if config.is_choice:
    behav_result['presented_target_ids'] = i_periph_targs
  else:
    behav_result['presented_target_ids'] = [i_presented_targ]

  async def fail_trial():
    await context.log('BehavState=fail')
    context.behav_result = behav_result
    show_presented_target = False
    state_brightness = 0    
    fail_sound.play()
    context.widget.update()

  while True:
    await context.log('BehavState=intertrial')
    state_brightness = 0
    
    context.widget.update()
    await context.sleep(config.intertrial_timeout)

    blank_space_touched = False
    await context.log('BehavState=start_on')
    state_brightness = 255
    show_presented_target = True
    context.widget.update()
    acquired = await wait_for(context, lambda: presented_targ_acquired or blank_space_touched, config.start_timeout)

    if acquired and not blank_space_touched:
      break
    else:
      show_presented_target = False
      context.widget.update()

  # state: startacq
  final_i_selected_target = last_selected_target
  behav_result['selected_target_id'] = final_i_selected_target
  await context.log('BehavState=start_acq')
  success = await wait_for_hold(context, lambda: presented_targ_acquired, config.hold_timeout, config.blink_timeout)
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  """
  The trial's outcome (success or failure) at this point is decided, and now
  we can wait (optionally) by success_timeout or fail_timeout.
  """
  show_presented_target = False
  context.widget.update()

  await context.log('BehavState=success')
  state_brightness = 0 

  on_time_ms = int(context.get_reward(all_reward_channels[final_i_selected_target]))

  signal = thalamus_pb2.AnalogResponse(
      data=[5,0],
      spans=[thalamus_pb2.Span(begin=0,end=2,name='Reward')],
      sample_intervals=[1_000_000*on_time_ms])

  await context.inject_analog('Reward', signal)

  success_sound.play()

  await context.sleep(config.success_timeout)

  context.behav_result = behav_result
  return task_context.TaskResult(True)
    

