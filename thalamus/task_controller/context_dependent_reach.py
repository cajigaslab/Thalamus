#pylint: skip-file
#type: ignore
"""
Implementation of the context dependent reach task
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
import PyQt5.QtCore
import PyQt5.QtWidgets
from PyQt5.QtGui import QColor
from PyQt5.QtMultimedia import QSound

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, RenderOutput, animate
from .. import task_controller_pb2
from ..config import ObservableCollection

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
  ('intertrial_timeout', datetime.timedelta),
  ('start_timeout', datetime.timedelta),
  ('baseline_timeout', datetime.timedelta),
  ('cue_display', datetime.timedelta),
  ('reach_timeout', datetime.timedelta),
  ('hold_timeout', datetime.timedelta),
  ('blink_timeout', datetime.timedelta),
  ('fail_timeout', datetime.timedelta),
  ('success_timeout', datetime.timedelta),
  ('border_color', QColor)  
])

RANDOM_DEFAULT = {'min': 1, 'max':1}
COLOR_DEFAULT = [255, 255, 255]

class TargetWidget(PyQt5.QtWidgets.QWidget):
  '''
  Widget for managing a target config
  '''
  def __init__(self, config: ObservableCollection) -> None:
    super().__init__()
    if 'name' not in config:
      config['name'] = 'Untitled'

    layout = PyQt5.QtWidgets.QGridLayout()
    self.setLayout(layout)

    layout.addWidget(PyQt5.QtWidgets.QLabel('Name:'), 0, 0)

    name_edit = PyQt5.QtWidgets.QLineEdit(config['name'])
    name_edit.setObjectName('name_edit')
    name_edit.textChanged.connect(lambda v: config.update({'name': v}))
    layout.addWidget(name_edit, 0, 1)

    def do_copy() -> None:
      if config.parent:
        config.parent.append(config.copy())

    copy_button = PyQt5.QtWidgets.QPushButton('Copy Target')
    copy_button.setObjectName('copy_button')
    copy_button.clicked.connect(do_copy)
    layout.addWidget(copy_button, 0, 2)

    fixed_form = Form.build(config, ['Name:', 'Value:'],
      Form.Constant('Width', 'width', 10, '\u00B0'),
      Form.Constant('Height', 'height', 10, '\u00B0'),
      Form.Constant('Orientation', 'orientation', 0, '\u00B0'),
      Form.Constant('Window Size', 'window_size', 0, '\u00B0'),
      Form.Constant('Reward Channel (if correct)', 'reward_channel', 0),
      Form.Constant('Audio Scale Left', 'audio_scale_left', 0),
      Form.Constant('Audio Scale Right', 'audio_scale_right', 0),
      Form.Color('Color', 'color', QColor(255, 255,255)),
      Form.Choice('Target Type', 'target_type', [('Fixation', 'fixation'), ('Cue', 'cue'), ('Response Target', 'response_target')]),
      Form.Constant('Mapping Assignment', 'mapping_assignment', 0), # 0 is null, i.e. this target is not correct for any mapping
      Form.Choice('Shape', 'shape', [('Box', 'box'), ('Ellipsoid', 'ellipsoid')]),
      Form.File('Stl File (Overrides shape)', 'stl_file', '', 'Select Stl File', '*.stl'),
      Form.File('Audio File', 'audio_file', '', 'Select Audio File', '*.wav'),
      Form.Bool('Only Play If Channel Is High', 'audio_only_if_high'),
      Form.Bool('Play In Ear', 'play_in_ear'),      
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

def create_widget(task_config: ObservableCollection) -> PyQt5.QtWidgets.QWidget:
  """
  Creates a widget for configuring the context dependent reach task
  """
  result = PyQt5.QtWidgets.QWidget()
  layout = PyQt5.QtWidgets.QVBoxLayout()
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
    Form.Uniform('Cue Display', 'cue_display', 1, 1, 's'),
    Form.Uniform('Reach Timeout', 'reach_timeout', 1, 1, 's'),
    Form.Uniform('Hold Interval', 'hold_timeout', 1, 1, 's'),
    Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Color('Border Color', 'border_color',QColor(255, 255,255))
  )
  layout.addWidget(form)

  new_target_button = PyQt5.QtWidgets.QPushButton('Add Target')
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
  d_m = 0.4 
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

    all_target_rects.append(PyQt5.QtCore.QRect(p_win[0] - targ_width_px/2, p_win[1] - targ_height_px/2, targ_width_px, targ_height_px))

  return all_target_rects

def get_start_target_index(context):
  all_target_configs = context.task_config['targets']
  
  i_start_targ = [itarg for itarg, x in enumerate(all_target_configs) if x['target_type'] == 'fixation']
  
  if len(i_start_targ) == 0:
    i_start_targ = [0]
  else:
    i_start_targ = i_start_targ[0]

  return i_start_targ

def distance(lhs, rhs):
  return ((lhs.x() - rhs.x())**2 + (lhs.y() - rhs.y())**2)**.5

async def stamp_msg(context, msg):
  msg.header.stamp = context.ros_manager.node.node.get_clock().now().to_msg()
  #context.pulse_digital_channel()
  return msg

def toggle_brightness(brightness):
  return 0 if brightness == 255 else 255

def get_cue_targ_indices(context):
  targets = context.task_config['targets']
  return [itarg for itarg, x in enumerate(targets) if x['target_type'] == 'cue']

def get_resp_targ_indices(context):
  targets = context.task_config['targets']
  return [itarg for itarg, x in enumerate(targets) if x['target_type'] == 'response_target']

def get_cue_sound(context, i_selected_cue):
  targets = context.task_config['targets']

  audio_file = targets[i_selected_cue]['audio_file']
  if audio_file:
    return QSound(audio_file)    
  else:
    return None

def get_cue_response_mappings(context):
  """
  returns a dictionary between cues (target index of cue) to responses (target index list)
  """
  targets = context.task_config['targets']

  # go through the targets, identify "cue" targets. for each cue target, look for other targets that have the same mapping index that are also response targets.
  cue_resp_map = {}
  for i, targ in enumerate(targets):
    if targ['target_type'] == 'cue':
      mapping_assignment = targ['mapping_assignment']
      response_targs_with_same_mapping_assignment = [itarg for itarg, x in enumerate(targets) 
        if x['mapping_assignment'] == mapping_assignment and 
          x['target_type'] == 'response_target']      
      cue_resp_map[i] = response_targs_with_same_mapping_assignment      
      
  return cue_resp_map

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
    datetime.timedelta(seconds=context.get_value('baseline_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('cue_dispay', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('reach_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('hold_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('blink_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('fail_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('success_timeout', RANDOM_DEFAULT)),   
    context.get_color('border_color', default=COLOR_DEFAULT)
  )

  """
  Identifying the "start" target, akin to the fixation target.
  The behavior defined as follows. Any target that is marked "is fixation" is
  the start target. If none of them are defined as the start target, then default to
  the first target in the target list. If multiple are selected, then choose the
  first that is selected.
  """
  ntargets = len(context.task_config['targets'])
  i_start_targ = get_start_target_index(context)
  i_periph_targs = [x for x in range(ntargets) if x is not i_start_targ]
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

  # randomly select a mapping from the available mappings  
  cue_targ_indices = get_cue_targ_indices(context)
  resp_targ_indices = get_resp_targ_indices(context)
  cue_resp_mapping = get_cue_response_mappings(context)
  ncues = len(cue_resp_mapping.keys())
  
  i_selected_cue = cue_targ_indices[np.random.randint(ncues)]

  cue_sound = get_cue_sound(context, i_selected_cue)
  
  i_correct_resp_targ = cue_resp_mapping[i_selected_cue][0] # decided to choose first matching response target
  correct_resp_targ_rect = all_target_rects[i_correct_resp_targ]


  start_target_acquired = False
  correct_resp_targ_acquired = False
  any_resp_targ_acquired = False
  i_selected_target = None
  last_selected_target = None
  touch_pos = PyQt5.QtCore.QPoint()
  def touch_handler(cursor: PyQt5.QtCore.QPoint) -> None:
    nonlocal start_target_acquired
    nonlocal correct_resp_targ_acquired
    nonlocal any_resp_targ_acquired
    nonlocal i_selected_target
    nonlocal last_selected_target
    nonlocal touch_pos
    start_target_acquired = distance(all_target_rects[i_start_targ].center(), cursor) < all_target_windows[i_start_targ]
    
    correct_resp_targ_acquired = distance(
      correct_resp_targ_rect.center(), cursor) < all_target_windows[i_correct_resp_targ]

    any_acquired = False
    for i_targ in resp_targ_indices + cue_targ_indices:
      is_in_window = distance(all_target_rects[i_targ].center(), cursor) < all_target_windows[i_targ]
      if is_in_window:
        i_selected_target = i_targ
        last_selected_target = i_selected_target
        any_acquired = True
        break
    any_resp_targ_acquired = any_acquired
    

    if correct_resp_targ_acquired:
      i_selected_target = i_correct_resp_targ
      last_selected_target = i_selected_target
    elif not any_acquired:
      i_selected_target = None
    touch_pos = cursor
  context.widget.touch_listener = touch_handler

  dim_start_target = False
  show_start_target = False
  show_cue = False
  show_response_targets = False
  display_state_brightness = 0
  def renderer(painter: PyQt5.QtGui.QPainter) -> None:
    color_base = all_target_colors[i_start_targ]
    scale = (all_target_on_luminance[i_start_targ] if not dim_start_target
             else all_target_off_luminance[i_start_targ])
    color_base = QColor(scale*color_base.red(), scale*color_base.green(), scale*color_base.blue())
    window = all_target_windows[i_start_targ]

    stl_mesh = all_target_stls[i_start_targ]

    border_width = 50
    painter.fillRect(0, 0, context.widget.width(), context.widget.height(), QColor(0, 0, 0))
    painter.fillRect(320, 140, context.widget.width()-640, context.widget.height()-280, config.border_color)
    #painter.fillRect(20, 20, context.widget.width() - border_width, context.widget.height() - border_width, config.border_color)
    painter.fillRect(350, 170, context.widget.width()-700, context.widget.height()-340, QColor(0, 0, 0))

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

    if show_cue:
      stl_mesh = all_target_stls[i_selected_cue]
      if stl_mesh:
        painter.render_stl(stl_mesh)
      else:
        painter.fillRect(all_target_rects[i_selected_cue], all_target_colors[i_selected_cue])
        
    if show_response_targets:

      for iresp in resp_targ_indices:
        stl_mesh = all_target_stls[iresp]
        if stl_mesh:
          painter.render_stl(stl_mesh)
        else:
          painter.fillRect(all_target_rects[iresp], all_target_colors[iresp])
        
    with painter.masked(RenderOutput.OPERATOR):
      path = PyQt5.QtGui.QPainterPath()
      
      path.addEllipse(all_target_rects[i_start_targ].center(), window, window)
      path.addEllipse(all_target_rects[i_correct_resp_targ].center(), window, window)

      painter.fillPath(path, QColor(255, 255, 255, 128))

    state_color = QColor(display_state_brightness, display_state_brightness, display_state_brightness)
    state_width = 40
    painter.fillRect(context.widget.width() - state_width, context.widget.height() - state_width, state_width, state_width, state_color)

    if show_touch_pos_feedback:
      cursor_color = QColor(255, 255, 255)
      cursor_width = 40
      painter.fillRect(touch_pos.x() - int(cursor_width/2), touch_pos.y() - int(cursor_width/2.0), cursor_width, cursor_width, cursor_color)
  context.widget.renderer = renderer

  behav_result = {}  
  behav_result['presented_target_ids'] = resp_targ_indices

  async def fail_trial():    
    nonlocal show_cue
    nonlocal show_response_targets
    nonlocal show_start_target
    nonlocal display_state_brightness
    context.behav_result = behav_result
    show_response_targets = False
    show_cue = False    
    show_start_target = False
    await context.servicer.publish_state(task_controller_pb2.BehavState(state='fail'))
    display_state_brightness = toggle_brightness(display_state_brightness)
    context.widget.update() 
    fail_sound.play()    
    
          
  while True:  
    await context.servicer.publish_state(task_controller_pb2.BehavState(state='intertrial'))
    display_state_brightness = 0
    show_start_target = False
    context.widget.update()
    await context.sleep(config.intertrial_timeout)

    # stat: start_on
    await context.servicer.publish_state(task_controller_pb2.BehavState(state='start_on'))
    display_state_brightness = toggle_brightness(display_state_brightness)
    show_start_target = True
    context.widget.update()
    acquired = await wait_for(context, lambda: start_target_acquired, config.start_timeout)

    if acquired:
      break
    else:
      show_start_target = False
      context.widget.update()

  # state: start_acq  
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='start_acq'))
  success = await wait_for_hold(context, lambda: start_target_acquired, config.baseline_timeout, config.blink_timeout)
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  # state: cue_on
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='cue_on'))
  display_state_brightness = toggle_brightness(display_state_brightness)
  show_cue = True
  if cue_sound:
    cue_sound.play()
  context.widget.update()
  success = await wait_for_hold(context, lambda: start_target_acquired, config.cue_display, config.blink_timeout )
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  if cue_sound:
    cue_sound.stop()

  # state: targs_on
  #show_cue = False
  #await context.servicer.publish_state(task_controller_pb2.BehavState(state='targs_on'))
  #display_state_brightness = toggle_brightness(display_state_brightness)
  #show_response_targets = True
  #context.widget.update()
  #success = await wait_for_hold(context, lambda: start_target_acquired, config.cue_display, config.blink_timeout)
  #if not success:
  #  await fail_trial()    
  #  await context.sleep(config.fail_timeout)
  #  return task_context.TaskResult(False)

  # state: go
  show_cue = False
  show_response_targets = True
  dim_start_target = True
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='go'))
  display_state_brightness = toggle_brightness(display_state_brightness)
  context.widget.update()
  acquired = await wait_for(context, lambda: any_resp_targ_acquired, config.reach_timeout)

  if not acquired:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  final_i_selected_target = last_selected_target
  behav_result['selected_target_id'] = final_i_selected_target

  # state: targs_acq
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targs_acq'))
  success = await wait_for_hold(context, lambda: correct_resp_targ_acquired, config.hold_timeout, config.blink_timeout)
  
  if not correct_resp_targ_acquired:
    context.behav_result = behav_result
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)
  
  """
  The trial's outcome (success or failure) at this point is decided, and now
  we can wait (optionally) by success_timeout or fail_timeout.
  """
  # show_cue = False
  show_response_targets = False  

  await context.servicer.publish_state(task_controller_pb2.BehavState(state='success'))
  display_state_brightness = toggle_brightness(display_state_brightness)
  context.widget.update()

  reward_message = RewardDeliveryCmd()
  reward_message.header.stamp = context.ros_manager.node.node.get_clock().now().to_msg()
  reward_message.on_time_ms = int(context.get_reward(all_reward_channels[final_i_selected_target]))
  
  print("delivering reward %d"%(reward_message.on_time_ms,) )
  context.publish(RewardDeliveryCmd, 'deliver_reward', reward_message)
    
  success_sound.play()      

  await context.sleep(config.success_timeout)

  context.behav_result = behav_result
  return task_context.TaskResult(True)
    
