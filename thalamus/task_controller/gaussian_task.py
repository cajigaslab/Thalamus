"""
Implementation of the Gaussian delayed saccade task
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
  ('fail_timeout', datetime.timedelta),
  ('decision_timeout', datetime.timedelta),
  ('fix1_timeout', datetime.timedelta),
  ('fix2_timeout', datetime.timedelta),
  ('blink_timeout', datetime.timedelta),
  ('success_timeout', datetime.timedelta),
  ('penalty_delay', datetime.timedelta),
  ('target_rectangle', QRect),
  ('target_color', QColor),
  ('shape', str)  # Add the shape attribute
])

RANDOM_DEFAULT = {'min': 1, 'max':1}
COLOR_DEFAULT = [255, 255, 255]
shapes = ['rectangle', 'gaussian'] # Define the possible shapes

#  Widget for managing the GUI fields that appear after pressing ADD TARGET
class TargetWidget(QWidget):

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
      Form.Constant('Width', 'width', 1, '\u00B0'),
      Form.Constant('Height', 'height', 1, '\u00B0'),
      Form.Constant('Orientation', 'orientation', 0, '\u00B0'),
      Form.Constant('Opacity', 'opacity', 1),
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
  if the key (e.g. decision_timeout) is not found in the task_config, the parameters will
  default to the values provided below. The build function also wires up all the
  listeners to update the task_config when changes are made.
  """
  form = Form.build(task_config, ["Name:", "Min:", "Max:"],
    Form.Constant('Target Width (0.1-1.0)', 'width', 1, '\u00B0'),
    Form.Constant('Target Height (0.1-1.0)', 'height', 1, '\u00B0'),
    Form.Constant('Orientation (0-150)', 'orientation', 0, '\u00B0'),
    Form.Constant('Opacity  (0-1.0)', 'opacity', 1),
    Form.Bool('Lock Height to Width?', 'is_height_locked', False),
    Form.Constant('Center X', 'center_x', 0, '\u00B0'),
    Form.Constant('Center Y', 'center_y', 0, '\u00B0'),
    Form.Uniform('Fixation Interval 1', 'fix1_timeout', 1, 2, 's'),
    Form.Uniform('Blink Interval', 'blink_timeout', 2, 4, 's'),
    Form.Uniform('Fixation Interval 2', 'fix2_timeout', 1, 2, 's'),
    Form.Uniform('Decision Interval', 'decision_timeout', 1, 2, 's'),
    Form.Uniform('Failure Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Uniform('Penalty Delay', 'penalty_delay', 3, 3, 's'),
    Form.Color('Target Color', 'target_color', QColor(255, 255, 255)),
    Form.Constant('Shape', 'shape',  random.choice(shapes))  # Add the shape attribute
  )
  layout.addWidget(form)

  # spinbox allows to constraint value options for above constants
  width_spinbox = form.findChild(QDoubleSpinBox, "width")
  width_spinbox.setRange(.1, 1.0)
  width_spinbox.setSingleStep(.1)
  height_spinbox = form.findChild(QDoubleSpinBox, "height")
  height_spinbox.setRange(.1, 1.0)
  height_spinbox.setSingleStep(.1)
  orientation_spinbox = form.findChild(QDoubleSpinBox, "orientation")
  orientation_spinbox.setRange(0, 150)
  orientation_spinbox.setSingleStep(30)
  opacity_spinbox = form.findChild(QDoubleSpinBox, "opacity")
  opacity_spinbox.setRange(.1, 1.0)
  opacity_spinbox.setSingleStep(.1)

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

async def run(context: TaskContextProtocol) -> TaskResult: #pylint: disable=too-many-statements
  """
  Implementation of the state machine for the simple task
  """

  """
  Below is an object that contains a realization generated by sampling from the random
  distributions defined in the task_config. It itself has no logic, it simply holds
  the realization's values.
  """
  raise RuntimeError('Remtoe Executor Only Task')