#pylint: skip-file
#type: ignore
"""
Implementation of the simple touch and look task. 
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
from .. import thalamus_pb2

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
  ('intertrial_timeout', datetime.timedelta),
  ('start_timeout', datetime.timedelta),
  ('baseline_timeout', datetime.timedelta),
  ('hand_blink', datetime.timedelta),
  ('eye_blink', datetime.timedelta),
  ('fail_timeout', datetime.timedelta),
  ('success_timeout', datetime.timedelta)
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
      Form.Choice('Target Type', 'target_type', [('Fixation', 'fixation'),]),   
      Form.Choice('Target Effector', 'target_effector', [('Touch', 'touch'), ('Gaze', 'gaze')]),
      Form.Choice('Shape', 'shape', [('Box', 'box'), ('Ellipsoid', 'ellipsoid')]),      
      Form.File('Audio File', 'audio_file', '', 'Select Audio File', '*.wav'),
      Form.Bool('Only Play If Channel Is High', 'audio_only_if_high'),
      Form.Bool('Play In Ear', 'play_in_ear')
    )
    layout.addWidget(fixed_form, 1, 1, 1, 2)

    random_form = Form.build(config, ['Name:', 'Min:', 'Max:'],
      Form.Uniform('Radius', 'radius', 0, 5, '\u00B0'),
      Form.Uniform('Angle', 'angle', 0, 360, '\u00B0'),      
    )
    layout.addWidget(random_form, 1, 3, 1, 2)

def create_widget(task_config: ObservableCollection) -> QWidget:
  print('creating widget')
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
    Form.Uniform('Touch Blink Interval', 'hand_blink', 1, 1, 's'),
    Form.Uniform('Gaze Blink Interval', 'eye_blink', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),    
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

  print('end creating widget')
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

def get_start_touch_target_index(context):
  all_target_configs = context.task_config['targets']
  i_start_targ = [itarg for itarg, x in enumerate(all_target_configs) if x['target_type'] == 'fixation' and x['target_effector'] == 'touch']      
  
  if len(i_start_targ) == 0:
    i_start_targ = 0
  else:
    i_start_targ = i_start_targ[0]

  return i_start_targ

def get_start_gaze_target_index(context):
  all_target_configs = context.task_config['targets']
  i_start_targ = [itarg for itarg, x in enumerate(all_target_configs) if x['target_type'] == 'fixation' and x['target_effector'] == 'gaze']    
  
  if len(i_start_targ) == 0:
    i_start_targ = 0
  else:
    i_start_targ = i_start_targ[0]

  return i_start_targ

def distance(lhs, rhs):
  return ((lhs.x() - rhs.x())**2 + (lhs.y() - rhs.y())**2)**.5

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
    datetime.timedelta(seconds=context.get_value('hand_blink', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('eye_blink', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('fail_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('success_timeout', RANDOM_DEFAULT)),        
  )

  """
  Identifying the "start" target, akin to the fixation target.
  The behavior defined as follows. Any target that is marked "is fixation" is
  the start target. If none of them are defined as the start target, then default to
  the first target in the target list. If multiple are selected, then choose the
  first that is selected.
  """
  
  ntargets = len(context.task_config['targets']) # including fixation and targ2s
  print('getting start target indices')
  i_start_touch_targ = get_start_touch_target_index(context)
  i_start_gaze_targ = get_start_gaze_target_index(context)
  
  dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()
  
  all_target_rects = get_target_rectangles(context, dpi)   
  all_target_windows = [ecc_to_px(context.get_target_value(itarg, 'window_size'), dpi)
                        for itarg in range(ntargets)]
  all_target_colors = [context.get_target_color(itarg, 'color', COLOR_DEFAULT) for itarg in range(ntargets)]
  all_target_names = [context.get_target_value(i, 'name', None) for i in range(ntargets)]
  all_reward_channels = [context.get_target_value(i, 'reward_channel', None) for i in range(ntargets)]
  
  """
  Defining drawing and cursor behavior.
  """

  start_target_touched = False
  start_target_gazed = False  
  last_touched_target = None    
  i_touched_target = None
  touch_pos = QPoint()  
  def touch_handler(cursor: QPoint) -> None:
    nonlocal start_target_touched    
    nonlocal i_touched_target
    nonlocal last_touched_target
    nonlocal touch_pos
    nonlocal start_target_gazed        

    # nonlocal start_target_touched_and_gazed 
    # nonlocal presented_targ_touched_and_gazed 
    # nonlocal targ2_touched_and_gazed 

    start_target_touched = distance(all_target_rects[i_start_touch_targ].center(), cursor) < all_target_windows[i_start_touch_targ]

    if start_target_touched:
      i_touched_target = i_start_touch_targ
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
  i_gazed_target = None
  gaze_pos = QPoint()
  def gaze_handler(cursor: QPoint) -> None:
    nonlocal start_target_gazed    
    nonlocal i_gazed_target
    nonlocal last_gazed_target
    nonlocal gaze_pos    
    # nonlocal start_target_touched_and_gazed 
    # nonlocal presented_targ_touched_and_gazed 
    # nonlocal targ2_touched_and_gazed 

    start_target_gazed = distance(all_target_rects[i_start_gaze_targ].center(), cursor) < all_target_windows[i_start_gaze_targ]    
    
    if start_target_gazed:
      i_gazed_target = i_start_gaze_targ
      last_gazed_target = i_gazed_target
    else:
      i_gazed_target = None
    gaze_pos = cursor

  context.widget.gaze_listener = gaze_handler
  
  show_start_target = False
  show_presented_target = False  
  state_brightness = 0
  def renderer(painter: QPainter) -> None:
    
        
    if show_start_target:      
      painter.fillRect(all_target_rects[i_start_touch_targ], all_target_colors[i_start_touch_targ])
      painter.fillRect(all_target_rects[i_start_gaze_targ], all_target_colors[i_start_gaze_targ])

    with painter.masked(RenderOutput.OPERATOR):
      path = QPainterPath()

      for i, rect in enumerate(all_target_rects):
        window = all_target_windows[i]
        path.addEllipse(QPointF(rect.center()), window, window)

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
  behav_result['touch_target_id'] = [i_start_touch_targ]
  behav_result['gaze_target_id'] = [i_start_gaze_targ]

  failed = False
  async def fail_trial():    
    nonlocal show_start_target        
    nonlocal state_brightness
    nonlocal failed
    failed = True
    show_start_target = False    
    context.behav_result = behav_result
    await context.log('BehavState=fail')
    state_brightness = toggle_brightness(state_brightness)
    fail_sound.play()
    context.widget.update()
          
  
  while True:
    await context.log('BehavState=intertrial')
    state_brightness = 0
    show_start_target = False
    context.widget.update()
    await context.sleep(config.intertrial_timeout)

    await context.log('BehavState=start_on')
    state_brightness = toggle_brightness(state_brightness)
    show_start_target = True
    context.widget.update()
    acquired = await wait_for(context, lambda: start_target_touched and start_target_gazed, config.start_timeout)

    if acquired:
      break
    else:
      show_start_target = False
      context.widget.update

  # state: startacq
  await context.log('BehavState=start_acq')
  success = await wait_for_dual_hold(context, config.baseline_timeout, 
    lambda: start_target_touched, lambda: start_target_gazed, 
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
  show_start_target = False
  await context.log('BehavState=success')
  state_brightness = toggle_brightness(state_brightness)
  context.widget.update()

  on_time_ms = int(context.get_reward(all_reward_channels[final_i_touched_target]))

  signal = thalamus_pb2.AnalogResponse(
      data=[5,0],
      spans=[thalamus_pb2.Span(begin=0,end=2,name='Reward')],
      sample_intervals=[1_000_000*on_time_ms])

  await context.inject_analog('Reward', signal)
    
  success_sound.play()      

  await context.sleep(config.success_timeout)

  context.behav_result = behav_result
  return task_context.TaskResult(True)
    
