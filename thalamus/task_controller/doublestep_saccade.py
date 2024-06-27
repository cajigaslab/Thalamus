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

import PyQt5.QtCore
import PyQt5.QtWidgets
from PyQt5.QtGui import QColor
from PyQt5.QtMultimedia import QSound

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, RenderOutput, animate
from . import task_controller_pb2

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
  ('intertrial_timeout', datetime.timedelta),
  ('start_timeout', datetime.timedelta),
  ('baseline_timeout', datetime.timedelta),
  ('cue_timeout', datetime.timedelta),
  ('targ2_delay', datetime.timedelta),
  ('saccade_timeout', datetime.timedelta),
  ('saccade2_timeout', datetime.timedelta),
  ('hold_timeout', datetime.timedelta),
  ('blink_timeout', datetime.timedelta),
  ('fail_timeout', datetime.timedelta),
  ('success_timeout', datetime.timedelta),
  ('targets_are_targ2s', bool),
])

RANDOM_DEFAULT = {'min': 1, 'max':1}
COLOR_DEFAULT = [255, 255, 255]

def validate_target(config, text):
    anchor_target = None
    for target in config.parent:
      if target is config:
        continue
      if target['name'] == text:
        if anchor_target is not None:
          asyncio.get_event_loop().call_soon(lambda: PyQt5.QtWidgets.QMessageBox.warning(None, 'Invalid Anchor', 'Multiple targets with that name exist'))
          return False
        else:
          anchor_target = target
    if anchor_target is None:
      asyncio.get_event_loop().call_soon(lambda: PyQt5.QtWidgets.QMessageBox.warning(None, 'Invalid Anchor', 'No target with that name exist'))
      return False
    return True


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

    fixed_form_layout = fixed_form.layout()
    assert isinstance(fixed_form_layout, PyQt5.QtWidgets.QGridLayout)

    anchor_target_widget = PyQt5.QtWidgets.QLineEdit()
    fixed_form_layout.addWidget(PyQt5.QtWidgets.QLabel('Anchor:'), fixed_form_layout.rowCount(), 0, 1, 1)
    fixed_form_layout.addWidget(anchor_target_widget, fixed_form_layout.rowCount()-1, 1, 1, 2)

    if 'anchor' not in config:
      config['anchor'] = ''
    anchor_target_widget.setText(config['anchor'])
    
    def on_anchor_widget_changed():
      text = anchor_target_widget.text()
      if text == '' or config['anchor'] == text:
        return
      if not validate_target(config, text) and text != '':
        anchor_target_widget.setText(config['anchor'])
        return
      config['anchor'] = text

    def on_anchor_config_changed(_, key: typing.Any, value: typing.Any) -> None:
      if key == 'anchor' and anchor_target_widget.text() != value:
        anchor_target_widget.setText(value)
    
    anchor_target_widget.editingFinished.connect(on_anchor_widget_changed)

    random_form = Form.build(config, ['Name:', 'Min:', 'Max:'],
      Form.Uniform('Radius', 'radius', 0, 5, '\u00B0'),
      Form.Uniform('Angle', 'angle', 0, 360, '\u00B0'),
      Form.Uniform('Audio Volume', 'volume', 0, 0),
      Form.Uniform('Auditory Temporal Jitter', 'auditory_temporal_jitter', 0, 0),
      Form.Uniform('Auditory Spatial Offset', 'auditory_spatial_offset', 0, 0),
      Form.Uniform('Auditory Spatial Offset Around Fixation', 'auditory_spatial_offset_around_fixation', 0, 0),
      Form.Uniform('On Luminance', 'on_luminance', 1, 1),
      Form.Uniform('Off Luminance', 'off_luminance', 0, 0),
    )
    layout.addWidget(random_form, 1, 3, 1, 2)

def create_widget(task_config: ObservableCollection) -> PyQt5.QtWidgets.QWidget:
  """
  Creates a widget for configuring the simple task
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
    Form.Uniform('Cue Interval', 'cue_timeout', 1, 1, 's'),
    Form.Uniform('Targ2 Delay', 'targ2_delay', 0.05, 0.2, 's'),
    Form.Uniform('Saccade Timeout', 'saccade_timeout', 1, 1, 's'),
    Form.Uniform('Saccade 2 Timeout', 'saccade2_timeout', 1, 1, 's'),
    Form.Uniform('Hold Interval', 'hold_timeout', 1, 1, 's'),
    Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Bool('Targets Are Second Targets', 'targets_are_targ2s', False),
    Form.Constant('State Indicator X', 'state_indicator_x', 180),
    Form.Constant('State Indicator Y', 'state_indicator_y', 0),
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
  d_m = 0.4 # meters (approximate, TODO: get proper measurement)
  x_m = d_m*np.tan(np.radians(ecc))
  x_inch = x_m/0.0254
  x_px = x_inch*dpi
  return x_px

def get_target_rectangle(context, itarg, dpi, cache):
  if cache[itarg] is not None:
    return cache[itarg]
  anchor_name = context.task_config['targets'][itarg]['anchor']

  canvas = context.widget
  x_ecc, y_ecc = pol_to_cart2d(
    context.get_target_value(itarg, 'radius'),
    context.get_target_value(itarg, 'angle'))

  targ_width_px = ecc_to_px(context.get_target_value(itarg, 'width'), dpi)
  targ_height_px = ecc_to_px(context.get_target_value(itarg, 'height'), dpi)

  anchor = [i for i in range(len(context.task_config['targets'])) 
            if context.task_config['targets'][i]['name'] == anchor_name]
  if anchor:
    anchor_itarg = anchor[0]

    anchored_rect = make_relative_targ2_rect(context, itarg, get_target_rectangle(context, anchor_itarg, dpi, cache), dpi)
    cache[itarg] = anchored_rect
    return anchored_rect

  ecc = np.array([x_ecc, y_ecc])

  # manually converting this offset to pixel coordinates
  pos_vis = ecc_to_px(ecc, dpi)
  t = np.array([canvas.frameGeometry().width()/2,
        canvas.frameGeometry().height()/2])
  Rvec = np.array([1.0, -1.0]) # manually specifying y axis flip

  p_win = Rvec*pos_vis + t

  result = PyQt5.QtCore.QRect(p_win[0] - targ_width_px/2, p_win[1] - targ_height_px/2, targ_width_px, targ_height_px)
  cache[itarg] = result
  return result

def get_target_rectangles(context, dpi):  

  ntargets = len(context.task_config['targets']) # all targets, including fixation and targ2s
  cache = [None]*ntargets
    
  all_target_rects = []  
  for itarg in range(ntargets): # looping through all targets, including fixation
    all_target_rects.append(get_target_rectangle(context, itarg, dpi, cache))
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

  return PyQt5.QtCore.QRect(p_win[0] - targ_width_px/2, p_win[1] - targ_height_px/2, targ_width_px, targ_height_px)


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
  show_gaze_pos_feedback = False
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
    datetime.timedelta(seconds=context.get_value('saccade_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('saccade2_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('hold_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('blink_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('fail_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('success_timeout', RANDOM_DEFAULT)),
    context.get_value('targets_are_targ2s'),
  )

  """
  Identifying the "start" target, akin to the fixation target.
  The behavior defined as follows. Any target that is marked "is fixation" is
  the start target. If none of them are defined as the start target, then default to
  the first target in the target list. If multiple are selected, then choose the
  first that is selected.
  """

  target_anchors = [t['anchor'] for t in context.task_config['targets']]
  target_names = [t['name'] for t in context.task_config['targets']]

  i_targs = [i for i, x in enumerate(context.task_config['targets']) if not x['is_targ2']] # including fixation, but not targ2s
  i_targ2s = [i for i, x in enumerate(context.task_config['targets']) if x['is_targ2'] and not x['anchor']] # only targ2s
  
  custom_display_state_x = int(context.task_config['state_indicator_x'])
  custom_display_state_y = int(context.task_config['state_indicator_y'])

  targ2s = [context.task_config['targets'][i] for i in i_targ2s]


  ntargets = len(context.task_config['targets'])
  ntarg2s = len(targ2s)
  i_start_targ = get_start_target_index(context)
  i_periph_targs = [i for i in i_targs if i is not i_start_targ]
  n_periph_targs = len(i_periph_targs)
  
  dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()

  all_target_rects = get_target_rectangles(context, dpi)
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
    targ2_rect = all_target_rects[i_presented_targ2]
  targ2_stl = all_target_stls[i_presented_targ2]
  targ2_color = all_target_colors[i_presented_targ2]
  all_target_rects[i_presented_targ2] = targ2_rect

  shown_targ2s = [i_presented_targ2]
  for i, target_anchor in enumerate(target_anchors):
    if target_anchor == target_names[i_presented_targ2]:
      shown_targ2s.append(i)

  start_target_acquired = False
  presented_targ_acquired = False
  i_selected_target = None
  last_selected_target = None
  targ2_acquired = False
  selected_targ2 = None
  gaze_pos = PyQt5.QtCore.QPoint()

  def gaze_handler(cursor: PyQt5.QtCore.QPoint) -> None:
    nonlocal start_target_acquired
    nonlocal presented_targ_acquired
    nonlocal i_selected_target
    nonlocal last_selected_target
    nonlocal targ2_acquired
    nonlocal gaze_pos
    nonlocal selected_targ2
    
    in_windows = [all_target_rects[i] is not None and distance(all_target_rects[i].center(), cursor) < all_target_windows[i]
                                  for i in range(ntargets)]

    start_target_acquired = distance(all_target_rects[i_start_targ].center(), cursor) < all_target_windows[i_start_targ]
    presented_targ_acquired = distance(
        all_target_rects[i_presented_targ].center(), cursor) < all_target_windows[i_presented_targ]

    targ2_distances = [i for i in shown_targ2s if distance(all_target_rects[i].center(), cursor) < all_target_windows[i]]
    targ2_acquired = bool(targ2_distances)
    selected_targ2 = targ2_distances[0] if targ2_acquired else None 
   
      
    if presented_targ_acquired:
      i_selected_target = i_presented_targ
      last_selected_target = i_selected_target
    else:
      i_selected_target = None
    
    gaze_pos = cursor


  context.widget.gaze_listener = gaze_handler

  dim_start_target = False
  show_start_target = False
  show_presented_target = False
  state_brightness = 0
  show_targ2_target = False

  def renderer(painter: PyQt5.QtGui.QPainter) -> None:
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
        for i in shown_targ2s:
          painter.fillRect(all_target_rects[i], all_target_colors[i])



    with painter.masked(RenderOutput.OPERATOR):
      path = PyQt5.QtGui.QPainterPath()

      for rect in (r for r in all_target_rects if r is not None):
        path.addEllipse(rect.center(), window, window)


      painter.fillPath(path, QColor(255, 255, 255, 128))

    state_color = QColor(state_brightness, state_brightness, state_brightness)
    state_width = 70
    painter.fillRect(custom_display_state_x, custom_display_state_y, state_width, state_width, state_color)

    if show_gaze_pos_feedback:
       cursor_color = QColor(200, 50, 50)
       cursor_width = 40
       painter.fillRect(gaze_pos.x() - int(cursor_width/2), gaze_pos.y() - int(cursor_width/2.0), cursor_width, cursor_width, cursor_color)
  
  context.widget.renderer = renderer

  behav_result = {}
  behav_result['presented_target_ids'] = [i_presented_targ]

  async def fail_trial():    
    nonlocal state_brightness
    nonlocal show_presented_target
    nonlocal show_start_target
    nonlocal show_targ2_target

    show_start_target = False
    show_presented_target = False
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
    context.widget.update()
    acquired = await wait_for(context, lambda: start_target_acquired, config.start_timeout)

    if acquired:
      break
    else:
      show_start_target = False
      context.widget.update

  # state: startacq
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='start_acq'))  
  success = await wait_for_hold(context, lambda: start_target_acquired, config.baseline_timeout, config.blink_timeout)
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targs_on'))  
  show_presented_target = True
  state_brightness = toggle_brightness(state_brightness)
  context.widget.update()
  success = await wait_for_hold(context, lambda: start_target_acquired, config.cue_timeout, config.blink_timeout)
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  dim_start_target = True
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='go'))  
  state_brightness = toggle_brightness(state_brightness)
  context.widget.update()

  start_targ_released = await wait_for(context, lambda: not start_target_acquired, config.saccade_timeout)
  #acquired = await wait_for(context, lambda: presented_targ_acquired, config.saccade_timeout)
  
  if not start_targ_released:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='saccde_start'))  

  acquired = await wait_for(context, lambda: presented_targ_acquired, config.saccade_timeout)    
  if not acquired:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)
  state_brightness = toggle_brightness(state_brightness)
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targs_acq'))  

  success = await wait_for_hold(context,
    lambda: presented_targ_acquired, 
    config.targ2_delay, 
    config.blink_timeout)
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)
  
  behav_result['presented_targ2_id'] = int(i_presented_targ2)
  show_targ2_target = True
  state_brightness = toggle_brightness(state_brightness)
  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targs2_on'))  
  context.widget.update()

  acquired = await wait_for(context, lambda: targ2_acquired, config.saccade2_timeout)
  if not acquired:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)

  await context.servicer.publish_state(task_controller_pb2.BehavState(state='targ2_acq'))  
  success = await wait_for_hold(context, lambda: targ2_acquired, 
    config.hold_timeout, config.blink_timeout)
    
  if not success:
    await fail_trial()
    await context.sleep(config.fail_timeout)
    return task_context.TaskResult(False)


  final_i_selected_target = int(last_selected_target)
  behav_result['selected_target_id'] = final_i_selected_target 
  
  
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
  reward_message.on_time_ms = int(context.get_reward(all_reward_channels[selected_targ2]))
  
  print("delivering reward %d"%(reward_message.on_time_ms,) )
  context.publish(RewardDeliveryCmd, 'deliver_reward', reward_message)
  
  success_sound.play()

  await context.sleep(config.success_timeout)   
  
  context.behav_result = behav_result
  return task_context.TaskResult(True)

