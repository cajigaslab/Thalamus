#pylint: skip-file
#type: ignore
"""
Implementation of the 
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
from .util import wait_for, wait_for_hold, wait_for_dual_hold, RenderOutput, animate
from .. import task_controller_pb2
from ..config import ObservableCollection

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
  ('intertrial_timeout', datetime.timedelta),
  ('start_timeout', datetime.timedelta),
  ('baseline_timeout', datetime.timedelta),
  ('cue_timeout', datetime.timedelta),
  ('targ2_delay', datetime.timedelta),
  ('reach_timeout', datetime.timedelta),
  ('saccade2_timeout', datetime.timedelta),
  ('hold_interval', datetime.timedelta),
  ('hand_blink', datetime.timedelta),
  ('eye_blink', datetime.timedelta),
  ('fail_timeout', datetime.timedelta),
  ('success_timeout', datetime.timedelta),  
  ('targets_are_targ2s', bool)
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
      Form.Constant('Width', 'width', 2.5, '\u00B0'),
      Form.Constant('Height', 'height', 2.5, '\u00B0'),
      Form.Constant('Orientation', 'orientation', 0, '\u00B0'),
      Form.Constant('Window Size', 'window_size', 0, '\u00B0'),
      Form.Constant('Reward Channel', 'reward_channel', 0),
      Form.Constant('Audio Scale Left', 'audio_scale_left', 0),
      Form.Constant('Audio Scale Right', 'audio_scale_right', 0),
      Form.Color('Color', 'color', QColor(255, 255,255)),
      Form.Bool('Is Fixation', 'is_fixation', False),
      Form.Bool('Is Target 2', 'is_targ2', False),
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
    Form.Uniform('Baseline Interval', 'baseline_timeout', 1, 1, 's'),
    Form.Uniform('Cue Interval', 'cue_timeout', 1, 1, 's'),
    Form.Uniform('Targ2 Delay', 'targ2_delay', 0.05, 0.2, 's'),
    Form.Uniform('Reach Timeout', 'reach_timeout', 1, 1, 's'),    
    Form.Uniform('Saccade 2 Timeout', 'saccade2_timeout', 1, 1, 's'),
    Form.Uniform('Hold Interval', 'hold_interval', 1, 1, 's'),
    Form.Uniform('Touch Blink Interval', 'hand_blink', 1, 1, 's'),
    Form.Uniform('Gaze Blink Interval', 'eye_blink', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Bool('Targets Are Second Targets', 'targets_are_targ2s', False)
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

  ntargets = len(context.task_config['targets']) # all targets, including fixation and targ2s
    
  all_target_rects = []  
  for itarg in range(ntargets): # looping through all targets, including fixation

    if not context.get_target_value(itarg, 'is_targ2'):
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

    
      all_target_rects.append(QRect(p_win[0] - targ_width_px/2, p_win[1] - targ_height_px/2, targ_width_px, targ_height_px))

    else:
      all_target_rects.append(None)
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

  return QRect(p_win[0] - targ_width_px/2, p_win[1] - targ_height_px/2, targ_width_px, targ_height_px)


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

async def stamp_msg(context, msg):
  msg.header.stamp = context.ros_manager.node.node.get_clock().now().to_msg()
  context.pulse_digital_channel()
  return msg

def toggle_brightness(brightness):
  return 0 if brightness == 255 else 255

@animate(30)
async def run(context: task_context.TaskContextProtocol) -> task_context.TaskResult: #pylint: disable=too-many-statements
  """
  Implementation of the state machine for the simple task
  """  
  success_sound = QSound(os.path.join(os.path.dirname(__file__), 
      'success_clip.wav'))
  fail_sound = QSound(os.path.join(os.path.dirname(__file__), 
      'failure_clip.wav'))
  show_touch_pos_feedback = True
  show_gaze_pos_feedback = True
  """
  Below is an object that contains a realization generated by sampling from the random
  distributions defined in the task_config. It itself has no logic, it simply holds
  the realization's values.
  """
  config = Config(
    datetime.timedelta(seconds=context.get_value('intertrial_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('start_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('baseline_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('cue_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('targ2_delay', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('reach_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('saccade2_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('hold_interval', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('hand_blink', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('eye_blink', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('fail_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('success_timeout', RANDOM_DEFAULT)),    
    context.get_value('targets_are_targ2s')
  )

  """
  Identifying the "start" target, akin to the fixation target.
  The behavior defined as follows. Any target that is marked "is fixation" is
  the start target. If none of them are defined as the start target, then default to
  the first target in the target list. If multiple are selected, then choose the
  first that is selected.
  """
  
  i_targs = [i for i, x in enumerate(context.task_config['targets']) if not x['is_targ2']] # including fixation, but not targ2s
  i_targ2s = [i for i, x in enumerate(context.task_config['targets']) if x['is_targ2']] # only targ2s
  
  targ2s = [context.task_config['targets'][i] for i in i_targ2s]

  ntargets = len(context.task_config['targets']) # including fixation and targ2s
  ntarg2s = len(targ2s)
  i_start_targ = get_start_target_index(context)
  i_periph_targs = [i for i in i_targs if i is not i_start_targ]

  n_periph_targs = len(i_periph_targs)

  dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()
  
  all_target_rects = get_target_rectangles(context, dpi) # fixation, all targets, all targ2s (None entry for targ2s for now)
  all_target_rects_no_targ2s = [all_target_rects[i] for i in i_targs]
  
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

  if config.targets_are_targ2s:
    i_presented_targ2 = np.random.choice(np.setdiff1d(i_periph_targs, i_presented_targ))
    targ2_rect = all_target_rects[i_presented_targ2]
  else:
    targ2_stl = None
    i_presented_targ2 = np.random.choice(i_targ2s)    
    targ2_rect = make_relative_targ2_rect(context, 
      i_presented_targ2, 
      all_target_rects[i_presented_targ], 
      dpi)
  targ2_stl = all_target_stls[i_presented_targ2]
  targ2_color = all_target_colors[i_presented_targ2]
  all_target_rects[i_presented_targ2] = targ2_rect

  start_target_touched = False
  last_touched_target = None
  presented_targ_touched = False
  targ2_touched = False
  i_touched_target = None
  touch_pos = QPoint()  
  def touch_handler(cursor: QPoint) -> None:
    nonlocal start_target_touched
    nonlocal presented_targ_touched
    nonlocal targ2_touched
    nonlocal i_touched_target
    nonlocal last_touched_target
    nonlocal touch_pos
    nonlocal start_target_gazed
    nonlocal presented_targ_gazed
    nonlocal targ2_gazed

    # nonlocal start_target_touched_and_gazed 
    # nonlocal presented_targ_touched_and_gazed 
    # nonlocal targ2_touched_and_gazed 

    start_target_touched = distance(all_target_rects[i_start_targ].center(), cursor) < all_target_windows[i_start_targ]
    
    presented_targ_touched = distance(
      all_target_rects[i_presented_targ].center(), cursor) < all_target_windows[i_presented_targ]

    targ2_touched = distance(
      all_target_rects[i_presented_targ2].center(), cursor) < all_target_windows[i_presented_targ2]    

    if presented_targ_touched:
      i_touched_target = i_presented_targ
      last_touched_target = i_touched_target
    else:
      i_touched_target = None
    touch_pos = cursor

    # start_target_touched_and_gazed = start_target_touched and start_target_gazed
    # presented_targ_touched_and_gazed = presented_targ_touched and presented_targ_gazed
    # targ2_touched_and_gazed = targ2_touched and targ2_gazed    
  context.widget.touch_listener = touch_handler


  start_target_gazed = False
  last_gazed_target = None
  presented_targ_gazed = False
  targ2_gazed = False
  i_gazed_target = None
  gaze_pos = QPoint()
  def gaze_handler(cursor: QPoint) -> None:
    nonlocal start_target_gazed
    nonlocal presented_targ_gazed
    nonlocal targ2_gazed
    nonlocal i_gazed_target
    nonlocal last_gazed_target
    nonlocal gaze_pos
    nonlocal start_target_touched
    nonlocal presented_targ_touched
    nonlocal targ2_touched
    # nonlocal start_target_touched_and_gazed 
    # nonlocal presented_targ_touched_and_gazed 
    # nonlocal targ2_touched_and_gazed 

    start_target_gazed = distance(all_target_rects[i_start_targ].center(), cursor) < all_target_windows[i_start_targ]
    
    presented_targ_gazed = distance(
      all_target_rects[i_presented_targ].center(), cursor) < all_target_windows[i_presented_targ]

    targ2_gazed = distance(
      all_target_rects[i_presented_targ2].center(), cursor) < all_target_windows[i_presented_targ2]    

    if presented_targ_touched:
      i_gazed_target = i_presented_targ
      last_gazed_target = i_gazed_target
    else:
      i_gazed_target = None
    gaze_pos = cursor

    # start target
    # start_target_touched_and_gazed = start_target_touched and start_target_gazed
    # presented_targ_touched_and_gazed = presented_targ_touched and presented_targ_gazed
    # targ2_touched_and_gazed = targ2_touched and targ2_gazed    
  context.widget.gaze_listener = gaze_handler


  dim_start_target = False
  show_start_target = False
  show_presented_target = False
  show_targ2_target = False
  state_brightness = 0
  def renderer(painter: QPainter) -> None:
    color_base = all_target_colors[i_start_targ]
    scale = (all_target_on_luminance[i_start_targ] if not dim_start_target
             else all_target_off_luminance[i_start_targ])
    color_base = QColor(scale*color_base.red(), scale*color_base.green(), scale*color_base.blue())
    window = all_target_windows[i_start_targ]

    stl_mesh = all_target_stls[i_start_targ]
    if show_start_target:
      if stl_mesh:
        angle = (100*time.time()) % 360
        painter.model_view.translate(0, 0, -10)
        painter.model_view.rotate(angle, 1/math.sqrt(3), 1/math.sqrt(3), 1/math.sqrt(3))
        painter.model_view.rotate(-90, 1, 0, 0)
        painter.model_view.scale(.1)
        painter.render_stl(stl_mesh, color_base)
      else:
        painter.fillRect(all_target_rects[i_start_targ], color_base)

    if show_presented_target:
      stl_mesh = all_target_stls[i_presented_targ]
      if stl_mesh:
        painter.render_stl(stl_mesh)
      else:
        painter.fillRect(all_target_rects[i_presented_targ], all_target_colors[i_presented_targ])
    
    if show_targ2_target:                
      if targ2_stl:
        painter.render_stl(targ2_stl)
      else:
        painter.fillRect(targ2_rect, targ2_color)        

    with painter.masked(RenderOutput.OPERATOR):
      path = QPainterPath()

      for rect in all_target_rects_no_targ2s:
        path.addEllipse(rect.center(), window, window)

      painter.fillPath(path, QColor(255, 255, 255, 128))

    state_color = QColor(state_brightness, state_brightness, state_brightness)
    state_width = 40
    painter.fillRect(context.widget.width() - state_width, context.widget.height() - state_width, state_width, state_width, state_color)

    if show_gaze_pos_feedback:
      cursor_color = QColor(200, 50, 50)
      cursor_width = 40
      painter.fillRect(gaze_pos.x() - int(cursor_width/2), gaze_pos.y() - int(cursor_width/2.0), cursor_width, cursor_width, cursor_color)
  
    if show_touch_pos_feedback:
      cursor_color = QColor(50, 200, 50)
      cursor_width = 20
      painter.fillRect(touch_pos.x() - int(cursor_width/2), touch_pos.y() - int(cursor_width/2.0), cursor_width, cursor_width, cursor_color)

  context.widget.renderer = renderer

  behav_result = {}  
  behav_result['presented_target_ids'] = [i_presented_targ]

  failed = False
  async def fail_trial():
    nonlocal show_start_target
    nonlocal show_presented_target
    nonlocal show_targ2_target
    nonlocal state_brightness
    nonlocal failed
    failed = True
    show_presented_target = False
    show_start_target = False
    show_targ2_target = False
    context.behav_result = behav_result
    await context.servicer.publish_state(task_controller_pb2.BehavState(state='fail'))
    state_brightness = toggle_brightness(state_brightness)
    fail_sound.play()
    context.widget.update()
          
  while True:
    await context.servicer.publish_state(task_controller_pb2.BehavState(state='intertrial'))
    state_brightness = 0
    show_start_target = False
    context.widget.update()
    await context.sleep(config.intertrial_timeout)

    await context.servicer.publish_state(task_controller_pb2.BehavState(state='start_on'))
    state_brightness = toggle_brightness(state_brightness)
    show_start_target = True
    show_presented_target = True
    context.widget.update()
    acquired = await wait_for(context, lambda: presented_targ_touched and start_target_gazed, config.start_timeout)

    if acquired:
      break
    else:
      show_start_target = False
      show_presented_target = False
      context.widget.update

  # state: startacq
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='start_acq'))
  #success = await wait_for_hold(context, lambda: start_target_touched and start_target_gazed, config.baseline_timeout, config.hand_blink)
  success = await wait_for_dual_hold(context, config.baseline_timeout, 
    lambda: presented_targ_touched, lambda: start_target_gazed, 
    config.hand_blink, config.eye_blink)
                            
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targs_on'))
  state_brightness = toggle_brightness(state_brightness)  
  show_presented_target = True
  context.widget.update()
  #success = await wait_for_hold(context, lambda: start_target_touched and start_target_gazed, config.cue_timeout, config.hand_blink)
  success = await wait_for_dual_hold(context, config.cue_timeout, 
    lambda: presented_targ_touched, lambda: start_target_gazed, 
    config.hand_blink, config.eye_blink)
  if not success:
    await fail_trial()    
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  dim_start_target = True
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='go'))
  state_brightness = toggle_brightness(state_brightness)
  context.widget.update()
  
  start_targ_released = await wait_for(context, lambda: not start_target_touched, config.reach_timeout)
  if not start_targ_released:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='reach_start'))
  
  acquired = await wait_for(context, lambda: presented_targ_touched and presented_targ_gazed, config.reach_timeout)    
  if not acquired:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targs_acq'))


  #success = await wait_for_hold(context, lambda: presented_targ_touched and presented_targ_gazed, config.targ2_delay, config.hand_blink)
  success = await wait_for_dual_hold(context, config.targ2_delay, 
    lambda: presented_targ_touched, lambda: presented_targ_gazed, 
    config.hand_blink, config.eye_blink)
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)


  behav_result['presented_targ2_id'] = int(i_presented_targ2)
  show_targ2_target = True
  state_brightness = toggle_brightness(state_brightness)
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targ2_on'))
  context.widget.update()

  acquired = await wait_for(context, lambda: targ2_gazed and presented_targ_touched, config.saccade2_timeout)
  if not acquired:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targ2_acq'))

  #success = await wait_for_hold(context, lambda: presented_targ_touched and targ2_gazed, config.hold_interval, config.hand_blink)
  success = await wait_for_dual_hold(context, config.hold_interval, 
    lambda: presented_targ_touched, lambda: targ2_gazed, 
    config.hand_blink, config.eye_blink)
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  final_i_touched_target = last_touched_target
  behav_result['selected_target_id'] = final_i_touched_target
  
  """
  The trial's outcome (success or failure) at this point is decided, and now
  we can wait (optionally) by success_timeout or fail_timeout.
  """
  show_presented_target = False
  show_targ2_target = False
  
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='success'))
  state_brightness = toggle_brightness(state_brightness)
  context.widget.update()
  reward_message = RewardDeliveryCmd()

  reward_message.header.stamp = context.ros_manager.node.node.get_clock().now().to_msg()
  reward_message.on_time_ms = int(context.get_reward(all_reward_channels[final_i_touched_target]))
  
  print("delivering reward %d"%(reward_message.on_time_ms,) )
  context.publish(RewardDeliveryCmd, 'deliver_reward', reward_message)
    
  success_sound.play()      

  await context.sleep(config.success_timeout)

  context.behav_result = behav_result
  return task_context.TaskResult(True)
    
