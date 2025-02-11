"""
Implementation of the Gaussian delayed saccade task v1.0 (2025/02/11)
"""
import time
import typing
import numbers
import logging
from datetime import timedelta
import random
import numpy as np # import Numpy to draw Gaussian

from ..qt import *

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol, animate, CanvasProtocol, create_task_with_exc_handling, RenderOutput
from .. import task_controller_pb2
from ..thalamus_pb2 import AnalogResponse, Span
from ..config import *

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
  ('decision_timeout', timedelta),
  ('fix1_duration', timedelta),
  ('fix2_duration', timedelta),
  ('targethold_duration', timedelta),
  ('target_present_dur', timedelta),
  ('penalty_delay', timedelta),
  ('cum_blinking_ms', timedelta)
])

RANDOM_DEFAULT = {'min': 1, 'max':1}
COLOR_DEFAULT = [255, 255, 255]
shapes = ['rectangle', 'gaussian', 'square'] # Define the possible shapes
global testing_locations
testing_locations = True # a toggle for testing the plotting of target locations

#  Widget for managing the GUI fields that appear after pressing ADD TARGET
class TargetWidget(QWidget):

  def __init__(self, config: ObservableCollection) -> None:
    super().__init__()
    if 'name' not in config:
      config['name'] = 'Untitled'

    # Set up the layout
    layout = QGridLayout()
    self.setLayout(layout)

    # Add a label for the name
    layout.addWidget(QLabel('Name:'), 0, 0)

    # Add a QLineEdit for the name and connect it to update the config
    name_edit = QLineEdit(config['name'])
    name_edit.setObjectName('name_edit')
    name_edit.textChanged.connect(lambda v: config.update({'name': v}))
    layout.addWidget(name_edit, 0, 1)

    # Define the copy function to duplicate the config
    def do_copy() -> None:
      if config.parent:
        config.parent.append(config.copy())

    # Add a button to copy the target and connect it to the copy function
    copy_button = QPushButton('Copy Target')
    copy_button.setObjectName('copy_button')
    copy_button.clicked.connect(do_copy)
    layout.addWidget(copy_button, 0, 2)

    # Build and add the fixed form with various attributes
    fixed_form = Form.build(config, ['Name:', 'Value:'],
      Form.Constant('Gaze acceptance \u2300 (0.1-2.0)', 'accptolerance_deg', 2, '\u00B0'),
      Form.Constant('Window Size', 'window_size', 0, '\u00B0'),
      Form.Constant('Reward Channel', 'reward_channel', 0),
      Form.Constant('Audio Scale Left', 'audio_scale_left', 0),
      Form.Constant('Audio Scale Right', 'audio_scale_right', 0),
      Form.Constant('Subject\'s distance to the screen', 'subj_dist_m', .57, 'm'),
      Form.Constant('Monitor\'s width', 'monitor_width_pix', 1920, 'pix'),
      Form.Constant('Monitor\'s height', 'monitor_height_pix', 1080, 'pix'),
      Form.Color('Color', 'color', QColor(255, 255,255)),
      Form.Bool('Is Fixation', 'is_fixation', False),
      Form.Choice('Shape', 'shape', shapes),  # Add the shape attribute
      Form.File('Stl File (Overrides shape)', 'stl_file', '', 'Select Stl File', '*.stl'),
      Form.File('Audio File', 'audio_file', '', 'Select Audio File', '*.wav'),
      Form.Bool('Only Play If Channel Is High', 'audio_only_if_high'),
      Form.Bool('Play In Ear', 'play_in_ear')
    )
    layout.addWidget(fixed_form, 1, 1, 1, 2)

    # Build and add the random form with various attributes
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
    Form.Uniform('\u2194Target width (0.1-1.0)', 'width_targ_deg', 0.1, 1, '\u00B0'),
    Form.Constant('\u2194Target width step (0.1-1.0)', 'widthtargdeg_step', 0.1, '\u00B0'),
    Form.Uniform('\u2195Target height (0.1-1.0)', 'height_targ_deg', 0.1, 1, '\u00B0'),
    Form.Constant('\u2195Target height step (0.1-1.0)', 'heighttargdeg_step', 0.1, '\u00B0'),
    Form.Bool('Lock Height to Width?', 'is_height_locked', False),
    Form.Uniform('\U0001F9EDOrientation (0-150)', 'orientation_ran', 0, 150, '\u00B0'),
    Form.Constant('\U0001F9EDOrientation step size (0-150)', 'orientation_step', 30, '\u00B0'),
    Form.Uniform('\U0001F526Luminence (0-100)', 'luminence_per', 10, 100,'%'),
    Form.Constant('\U0001F526Luminence step size (0-100)', 'luminence_step', 10,'%'),
    Form.Constant('Gaze acceptance \u2300 (0.1-2.0)', 'accptolerance_deg', 2, '\u00B0'), # Define the diameter in degrees of the area where gaze is accepted as being correct
    Form.Constant('Subject\'s distance to the screen', 'subj_dist_m', .57, 'm'),
    Form.Constant('Monitor\'s width', 'monitor_width_m', .5283, 'm'),
    Form.Constant('Monitor\'s width', 'monitor_width_pix', 1920, 'pix'),
    Form.Constant('Monitor\'s height', 'monitor_height_pix', 1080, 'pix'),
    Form.Uniform('Fixation Duration 1', 'fix1_duration', 1000, 2000, 'ms'),
    Form.Uniform('Target Presentation Duration', 'target_present_dur', 2000, 4000, 'ms'), 
    Form.Uniform('Fixation Duration 2', 'fix2_duration', 1000, 2000, 'ms'),
    Form.Uniform('Target Hold Duration', 'targethold_duration', 1000, 2000, 'ms'),
    Form.Uniform('Decision Temeout', 'decision_timeout', 1000, 2000, 'ms'),
    Form.Uniform('Penalty Delay', 'penalty_delay', 3000, 3000, 'ms'),
    Form.Constant('Allowed cumulative blinking', 'cum_blinking_ms', 500, 'ms'),
    Form.Color('Target Color', 'target_color', QColor(255, 255, 255)),
    Form.Color('Background Color', 'background_color', QColor(128, 128, 128, 255)),
    Form.Choice('Shape', 'shape', list(zip(shapes, shapes))),  # Add the shape attribute
  )
  layout.addWidget(form)

  # spinbox allows to constraint value options for above constants
  monitor_width_pix_spinbox = form.findChild(QDoubleSpinBox, "monitor_width_pix")
  monitor_width_pix_spinbox.setRange(100, 4000)
  monitor_width_pix_spinbox.setSingleStep(10)
  monitor_height_pix_spinbox = form.findChild(QDoubleSpinBox, "monitor_height_pix")
  monitor_height_pix_spinbox.setRange(100, 4000)
  monitor_height_pix_spinbox.setSingleStep(10)
  widthtargdeg_step_spinbox = form.findChild(QDoubleSpinBox, "widthtargdeg_step")
  widthtargdeg_step_spinbox.setRange(.1, 1.0)
  widthtargdeg_step_spinbox.setSingleStep(.1)
  heighttargdeg_step_spinbox = form.findChild(QDoubleSpinBox, "heighttargdeg_step")
  heighttargdeg_step_spinbox.setRange(.1, 1.0)
  heighttargdeg_step_spinbox.setSingleStep(.1)
  orientation_step_spinbox = form.findChild(QDoubleSpinBox, "orientation_step")
  orientation_step_spinbox.setRange(1, 150)
  orientation_step_spinbox.setSingleStep(1)
  orientation_ran_min_spinbox = form.findChild(QDoubleSpinBox, "orientation_ran_min")
  orientation_ran_min_spinbox.setRange(0, 150)
  orientation_ran_min_spinbox.setSingleStep(15)  
  orientation_ran_max_spinbox = form.findChild(QDoubleSpinBox, "orientation_ran_max")
  orientation_ran_max_spinbox.setRange(0, 150)
  orientation_ran_max_spinbox.setSingleStep(15)
  accptolerance_deg_spinbox = form.findChild(QDoubleSpinBox, "accptolerance_deg")
  accptolerance_deg_spinbox.setRange(.1, 2.0)
  accptolerance_deg_spinbox.setSingleStep(0.1)
  luminence_step_spinbox = form.findChild(QDoubleSpinBox, "luminence_step")
  luminence_step_spinbox.setRange(1, 100)
  luminence_step_spinbox.setSingleStep(1)
  luminence_per_min_spinbox = form.findChild(QDoubleSpinBox, "luminence_per_min")
  luminence_per_min_spinbox.setRange(0, 100)
  luminence_per_min_spinbox.setSingleStep(5)  
  orientation_ran_max_spinbox = form.findChild(QDoubleSpinBox, "luminence_per_max")
  orientation_ran_max_spinbox.setRange(0, 100)
  orientation_ran_max_spinbox.setSingleStep(5)  
  width_targ_deg_min_spinbox = form.findChild(QDoubleSpinBox, "width_targ_deg_min")
  width_targ_deg_min_spinbox.setRange(0.1, 1)
  width_targ_deg_min_spinbox.setSingleStep(0.1)  
  width_targ_deg_max_spinbox = form.findChild(QDoubleSpinBox, "width_targ_deg_max")
  width_targ_deg_max_spinbox.setRange(0.1, 1)
  width_targ_deg_max_spinbox.setSingleStep(0.1)  
  height_targ_deg_min_spinbox = form.findChild(QDoubleSpinBox, "height_targ_deg_min")
  height_targ_deg_min_spinbox.setRange(0.1, 1)
  height_targ_deg_min_spinbox.setSingleStep(0.1)  
  height_targ_deg_max_spinbox = form.findChild(QDoubleSpinBox, "height_targ_deg_max")
  height_targ_deg_max_spinbox.setRange(0.1, 1)
  height_targ_deg_max_spinbox.setSingleStep(0.1)  

  # Add a button to add a new target to the task configuration
  new_target_button = QPushButton('Add Target')
  new_target_button.setObjectName('new_target_button')
  new_target_button.clicked.connect(lambda: task_config['targets'].append({}) and None)
  layout.addWidget(new_target_button)

  # Ensure the 'targets' key exists in the task configuration
  if 'targets' not in task_config:
    task_config['targets'] = []
  target_config_list = task_config['targets']
  
  # Create a tabbed widget to display and edit each target's configuration
  target_tabs = ListAsTabsWidget(target_config_list, TargetWidget, lambda t: str(t['name']))
  layout.addWidget(target_tabs)

  return result
# Define the framerate and frame interval for the task
FRAMERATE = 60
INTERVAL = 1/FRAMERATE

class Size(typing.NamedTuple):
  width: int
  height: int

class Converter:
  def __init__(self, screen_pixels: Size, screen_width_m: float, screen_distance_m: float):
    # Initialize the screen parameters
    self.screen_pixels = screen_pixels
    self.screen_width_m = screen_width_m
    self.screen_distance_m = screen_distance_m
    # Calculate the screen width in radians
    self.screen_width_rad = 2*np.arctan2(screen_width_m/2, screen_distance_m)
    # Calculate radians per pixel
    self.rad_per_pixel = self.screen_width_rad/screen_pixels.width
    # Calculate degrees per pixel
    self.deg_per_pixel = 180/np.pi*self.rad_per_pixel
    # Calculate meters per pixel
    self.m_per_pixel = screen_width_m/screen_pixels.width

  def deg_to_pixel_abs(self, *args) -> typing.Tuple[int, int]:
    # Convert degrees to absolute pixel coordinates (relative means that center of the screen is [0, 0])
    result = self.deg_to_pixel_rel(*args)
    # Coordinates are relative to the center of the screen, so add the screen center to get absolute coordinates
    if len(result) == 2:
      return result[0] + self.screen_pixels.width/2, result[1] + self.screen_pixels.height/2,
    else:
      return result[0] + self.screen_pixels.width/2

  def deg_to_pixel_rel(self, *args) -> typing.Tuple[int, int]:
    # Convert degrees to relative pixel coordinates
    if len(args) == 1: # Handle single argument case
      if isinstance(args[0], numbers.Number):
        return args[0]/self.deg_per_pixel
      else:
        x, y = args[0][0], args[0][1]
    else:
      x, y = args[0], args[1]
    return x/self.deg_per_pixel, y/self.deg_per_pixel

  def relpix_to_absdeg(self, *args) -> typing.Tuple[int, int]:
    # Convert relative pixels to absolute degrees (absolute means relative to the corner of the screen rather than the center)
    if len(args) == 1: # Handle single argument case
      if isinstance(args[0], numbers.Number):
        return (args[0] + self.screen_pixels.width/2)*self.deg_per_pixel
      else:
        x, y = args[0][0] + self.screen_pixels.width/2, args[0][1] + self.screen_pixels.height/2
    else:
      x, y = args[0] + self.screen_pixels.width/2, args[1] + self.screen_pixels.height/2
    return x*self.deg_per_pixel, y*self.deg_per_pixel

def gaussian_gradient(center: QPointF, gradient_background: QColor, radius: float, deviations: float = 1, brightness: int = 255):
  gradient = QRadialGradient(center, radius)
  resolution = 1000
  for i in range(resolution):
    # level = int(brightness*np.exp(-(deviations*i/resolution)**2/(2)))
    level = int(gradient_background[0] + (brightness - gradient_background[0])*np.exp(-(deviations*i/resolution)**2/(2))) # version that makes Gaussian colors bound by background
    # gradient_background[0] is used here to blend well with background color
    gradient.setColorAt(i/resolution, QColor(level, level, level))
    # !!The background of the square I draw the gaussian into is a little grey and it's pretty noticeable if it doesn't 
    # cover the whole screen.  I added this clipping to prevent that but it's also pretty noticeable. We may have to 
    # work on that!!!
  gradient.setColorAt(1, QColor(gradient_background[0], gradient_background[1], gradient_background[2], 255)) #Qt.GlobalColor.black
  return gradient

# Define an enumeration for the different states of a task
class State(enum.Enum):
  ACQUIRE_FIXATION = enum.auto()
  FIXATE1 = enum.auto()
  TARGET_PRESENTATION = enum.auto()
  FIXATE2 = enum.auto()
  ACQUIRE_TARGET = enum.auto()
  HOLD_TARGET = enum.auto()
  SUCCESS = enum.auto()
  FAILURE = enum.auto()

converter = None
center = None
center_f = None
num_circles = 7 # The last 2 circles usually end up being too large for the screen height, hence the actual # = num_circles - 2
circle_radii = []
rand_pos_i = 0
trial_num = 0
trial_success_count = 0
drawn_objects = []
rand_pos = []
gaze_success_store = []
gaze_failure_store = []

@animate(60) # This line allows the gaze to be sampled at 60 FPS this also allows painting 
# the gaze position in the Operator View continuously (i.e. continuously calling "widget.update()")
# without this line we would need to manually update the widget ("widget.update()") 
# to update the gaze position. "renderer()" will continue running indefinitely until the task is stopped
# or animation is stopped via QTimer() or condition. Calling "widget.update()" again
# rechecks current state and repaints widget based on current state without ever stopping the animation.
# Define an asynchronous function to run the task with a 60 FPS animation
async def run(context: TaskContextProtocol) -> TaskResult: #pylint: disable=too-many-statements
  global converter, center, center_f, num_circles, circle_radii, rand_pos_i, \
    trial_num, trial_success_count, drawn_objects, rand_pos, gaze_success_store, gaze_failure_store
  """
  Implementation of the state machine for the simple task
  """
  # Get the task configuration
  config = context.task_config
  monitor_width_pix = config['monitor_width_pix']
  monitor_height_pix = config['monitor_height_pix']
  subj_dist_m = config['subj_dist_m']
  monitor_width_m = config['monitor_width_m']

  if converter is None:
    # converter = Converter(Size(1920, 1080), .5283, .57)
    converter = Converter(Size(monitor_width_pix, monitor_height_pix), monitor_width_m, subj_dist_m)
    center = QPoint(int(converter.screen_pixels.width), int(converter.screen_pixels.height))/2
    center_f = QPointF(float(converter.screen_pixels.width), float(converter.screen_pixels.height))/2
    num_circles = 7 # The last 2 circles usually end up being too large for the screen height, hence the actual # = num_circles - 2
    circle_radii = np.linspace(0, converter.screen_pixels.height, 10) # screen height-based step
    circle_radii += converter.screen_pixels.width/num_circles # a sum of screen width and height based steps
    circle_radii = circle_radii[circle_radii <= converter.screen_pixels.height] # getting rid of radii that are too large for the screen height
    circle_radii /= 2 # divide by 2 to get the average of the two steps to ensure Gaussians are less squished
    rand_pos_i = 0
    trial_num = 0
    # Generate random positions around a circle
    rand_pos = [
      (center.x() + radius*np.cos(angle), center.y() + radius*np.sin(angle))
      for radius in circle_radii
      for angle in np.arange(0, 2*np.pi, np.pi/6)
    ]
    random.shuffle(rand_pos) # Shuffle the list of random positions to randomize their order

  """
  Below is an object that contains a realization generated by sampling from the random
  distributions defined in the task_config. It itself has no logic, it simply holds
  the realization's values.
  """

  trial_num += 1 # Increment the trial counter
  print(f"Started trial # {trial_num}")
  await context.log(f"StartedTRIAL_NUM={trial_num}") # saving any variables / data from code

  current_directory = os.getcwd() # Get the current working directory
  # Define a relative path (e.g., accessing a file in a subdirectory)
  relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'failure_clip.wav')
  failure_sound = QSound(relative_path) # Load the .wav file (replace with your file path)
  relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'success_clip.wav')
  success_sound = QSound(relative_path) # Load the .wav file (replace with your file path)

  # If all random positions have been used, shuffle the list and reset the index
  if rand_pos_i == len(rand_pos):
    random.shuffle(rand_pos)
    rand_pos_i = 0
  # Get the current target position from the list of random positions
  targetpos_pix = QPoint(int(rand_pos[rand_pos_i][0]), int(rand_pos[rand_pos_i][1]))
  context.trial_summary_data.used_values['targetposX_pix'] = targetpos_pix.x()
  context.trial_summary_data.used_values['targetposY_pix'] = targetpos_pix.y()
  targetpos_f = QPointF(int(rand_pos[rand_pos_i][0]), int(rand_pos[rand_pos_i][1]))
  current_rand_pos_i = rand_pos_i
  rand_pos_i += 1

  # Define the vertices for the fixation cross in degrees
  vertices_deg = [ 
      (-0.25, 0), (0.25, 0),  # Horizontal line
      (0, -0.25), (0, 0.25)  # Vertical line
  ]
  # Convert the vertices from degrees to pixels
  vertices = [converter.deg_to_pixel_abs(p) for p in vertices_deg]
  # Create a QPainterPath for the fixation cross
  cross = QPainterPath()
  cross.moveTo(vertices[0][0], vertices[0][1])
  cross.lineTo(vertices[1][0], vertices[1][1])
  cross.moveTo(vertices[2][0], vertices[2][1])
  cross.lineTo(vertices[3][0], vertices[3][1])
  
  # Define the size of the square in degrees and convert to pixels
  square_size_deg = .5
  square_size = converter.deg_to_pixel_rel(square_size_deg)

  # Get variables from the config
  accptolerance_deg = config['accptolerance_deg']
  accptolerance_pix = converter.deg_to_pixel_rel(accptolerance_deg)
  is_height_locked = config['is_height_locked']
  target_color_rgb = config['target_color']
  background_color = config['background_color']
  background_color_qt = QColor(background_color[0], background_color[1], background_color[2], 255)
  
  # Get various timeouts from the context (user GUI)
  target_present_dur = context.get_value('target_present_dur') / 1000 # dividing by 1000x to convert from ms to s
  decision_timeout = context.get_value('decision_timeout') / 1000
  fix1_duration = context.get_value('fix1_duration') / 1000
  fix2_duration = context.get_value('fix2_duration') / 1000
  targethold_duration = context.get_value('targethold_duration') / 1000
  penalty_delay = context.get_value('penalty_delay') / 1000
  cum_blinking_ms = context.get_value('cum_blinking_ms') / 1000

  def pick_random_value(min_val, max_val, step):
    # Generate the range of possible values using numpy
    possible_values = np.arange(min_val, max_val + step, step).tolist()
    # Pick a random value from the possible values
    return random.choice(possible_values)

  # Create a Gaussian gradient for the target with randomly selected luminance, orientation, size and location
  # Random selection is based on user-defined range min...max and step size
  luminence_per = pick_random_value(config['luminence_per']['min'], config['luminence_per']['max'], config['luminence_step'])
  context.trial_summary_data.used_values['luminence_per'] = luminence_per # this command based on 'get_value()' from 'task_context.py' aalows to add values to task_config['used_values']
  orientation_ran = pick_random_value(config['orientation_ran']['min'], config['orientation_ran']['max'], config['orientation_step'])
  context.trial_summary_data.used_values['orientation_ran'] = orientation_ran 
  width_targ_pix = converter.deg_to_pixel_rel(pick_random_value(config['width_targ_deg']['min'], config['width_targ_deg']['max'], config['widthtargdeg_step']))
  context.trial_summary_data.used_values['width_targ_pix'] = width_targ_pix 
  if is_height_locked:
    height_targ_pix = width_targ_pix
  else:
    height_targ_pix = converter.deg_to_pixel_rel(pick_random_value(config['height_targ_deg']['min'], config['height_targ_deg']['max'], config['heighttargdeg_step']))
  context.trial_summary_data.used_values['height_targ_pix'] = height_targ_pix 


  gaussian = gaussian_gradient(QPointF(0, 0), background_color, width_targ_pix/2, 3, luminence_per*255/100)
  def draw_gaussian(painter: QPainter):
    if testing_locations:
      # region -- TESTING-TARGET-LOCATIONS: Drawing version that preserves every plotted Gaussian
      drawn_objects.append({
        'position': targetpos_pix,
        'orientation': orientation_ran,
        'width': width_targ_pix,
        'height': height_targ_pix,
        'gradient': gaussian
      })
      for gaussian_obj in drawn_objects:
        painter.save()
        painter.translate(gaussian_obj['position'])
        painter.rotate(gaussian_obj['orientation'])
        painter.scale(1, gaussian_obj['height'] / gaussian_obj['width'])
        painter.fillRect(
            int(-gaussian_obj['width'] / 2),
            int(-gaussian_obj['height'] / 2),
            int(gaussian_obj['width']),
            int(gaussian_obj['height']),
            gaussian_obj['gradient']
        )
        painter.restore()
      # endregion
    else:
      # region -- Drawing version that plots only 1 current Gaussian
      painter.save()
      painter.translate(targetpos_pix)
      painter.rotate(orientation_ran) #Apply rotation to gaussian
      painter.scale(1, height_targ_pix/width_targ_pix) # Apply X,Y scaling to gaussian; initial diameter is equal to width/2, hence need to scale only Y
      painter.fillRect(int(-widget.width()/2), int(-widget.height()/2), int(widget.width()), int(widget.height()), gaussian)
      # painter.fillRect draws the gaussian into a giant rectangle centered at the upper left corner of the screen 
      # and the above lines will stretch, rotate (if uncommented), and translate the gaussian to the correct place.
      painter.restore()
      # endregion

  def draw_gaze(painter, gaze_qpoint, color_rgba):
    path = QPainterPath()
    gaze_f = QPointF(gaze_qpoint)
    path.addEllipse(gaze_f, 12, 12)
    painter.fillPath(path, color_rgba) 

  # Initialize the state to ACQUIRE_FIXATION
  state = State.ACQUIRE_FIXATION
  await context.log('BehavState=ACQUIRE_FIXATION_post-drawing') # saving any variables / data from code
  widget: CanvasProtocol = context.widget
  assert widget is not None

  # Initialize gaze position
  gaze = QPoint(0,0)
  def gaze_handler(cursor: QPoint) -> None:
    nonlocal gaze
    gaze = cursor

  # Set the gaze listener to the gaze handler function
  widget.gaze_listener = gaze_handler
  
  # These 4 lines of codes allow to clear from the operator's view
  # the painted gaze endpoints by pressing the space bar (code from "motion_capture_task.py")
  def on_key_release(e: QKeyEvent):
    if e.key() == Qt.Key.Key_Space:
      gaze_failure_store.clear()
      gaze_success_store.clear()
  context.widget.key_release_handler = on_key_release

  start = time.perf_counter() # Get the current time

  def renderer(painter: QPainter):
    nonlocal gaussian
    painter.fillRect(QRect(0, 0, 4000, 4000), background_color_qt) # QColor(128, 128, 128, 255); make the background of desired color
    painter.fillRect(int(widget.width() - 150), int(widget.height() - 150), 150, 150, QColor(0, 0, 0, 255)) # background small square bottom-right

    # Plotting of any variables using Thalamus' pipeline QT plots
    create_task_with_exc_handling(context.inject_analog('performance', AnalogResponse(
      data = [trial_success_count/trial_num*100, float(trial_num)],
      spans=[Span(begin=0, end=1, name='Photic trial success rate (%)'), Span(begin=1, end=2, name='Trial Number')],
      sample_intervals=[0, 0]
    )))

    # photodiode_square = QColor(255, 255, 255, 255) # Create a QColor object with white color and transparency control (i.e. alpha)
    # painter.fillRect(int(widget.width() - 100), int(widget.height()-100), 100, 100, photodiode_square) # Keep uncommented if want a constant white square for the photo-diode

    # Draw the fixation cross and Gaussian based on the current state
    if state in (State.ACQUIRE_FIXATION, State.FIXATE1):
      pen = painter.pen()
      pen.setWidth(3)
      pen.setColor(Qt.GlobalColor.red)
      painter.setPen(pen)
      # draw_gaussian(painter)
      painter.drawPath(cross)
      
      if testing_locations:
        # region -- TESTING-TARGET-LOCATIONS: drawing of the concentric circles and XY axes
        # Dynamically calculate the center of the window
        pen.setWidth(1)
        pen.setColor(QColor(0, 0, 255, 100))  # Set color to blue with 50% transparency (alpha = 100)
        painter.setPen(pen)
        center_x = int(converter.screen_pixels.width / 2)
        center_y = int(converter.screen_pixels.height / 2)
        # Draw the concentric circles
        for radius in circle_radii:
            painter.drawEllipse(QPointF(center_x, center_y), radius, radius)
        # Draw the center cross (XY axes)
        painter.drawLine(center_x, 0, center_x, converter.screen_pixels.height)  # Vertical line
        painter.drawLine(0, center_y, int(converter.screen_pixels.width), center_y)  # Horizontal line
        # Draw angled lines for 30° increments
        for angle in np.arange(0, 360, 30):
            x = int(center_x + circle_radii[-1] * np.cos(np.radians(angle)))
            y = int(center_y + circle_radii[-1] * np.sin(np.radians(angle)))
            painter.drawLine(center_x, center_y, x, y)
        # endregion

    elif state == State.FIXATE2:
      # await context.log('BehavState=FIXATE2_start') # saving any variables / data from code
      pen = painter.pen()
      pen.setWidth(3)
      pen.setColor(Qt.GlobalColor.red)
      painter.setPen(pen)
      painter.drawPath(cross)

    elif state == State.TARGET_PRESENTATION:
      # await context.log('BehavState=TARGET_PRESENTATION_start') # saving any variables / data from code
      pen = painter.pen()
      pen.setWidth(3)
      pen.setColor(Qt.GlobalColor.red)
      painter.setPen(pen)
      draw_gaussian(painter)
      painter.drawPath(cross)
      photodiode_square = QColor(255, 255, 255, 255) # Create a QColor object with white color and transparency control (i.e. alpha)
      painter.fillRect(int(widget.width() - 100), int(widget.height()-100), 100, 100, photodiode_square)

    elif state == State.HOLD_TARGET:
      photodiode_square = QColor(255, 255, 255, 255) # Create a QColor object with white color and transparency control (i.e. alpha)
      painter.fillRect(int(widget.width() - 100), int(widget.height()-100), 100, 100, photodiode_square)

    # Draw the shadings around targets in the OPERATOR view indicating the areas where responses are accepted as correct
    with painter.masked(RenderOutput.OPERATOR): # using a context manager to temporarily change the drawing behavior of the painter object
      # A feature of the operator view is that you can draw stuff only for the operator into it. Anything in this 
      # with painter.masked(RenderOutput.OPERATOR) block will only appear in the operator view.
      
      path = QPainterPath()
      path.addEllipse(targetpos_f, accptolerance_pix, accptolerance_pix)
      painter.fillPath(path, QColor(255, 255, 255, 128))
      path = QPainterPath()
      path.addEllipse(center_f, accptolerance_pix, accptolerance_pix)
      painter.fillPath(path, QColor(255, 255, 255, 128))

      # Drawing the gaze position as a continuously moving point
      color_rgba = QColor(0, 206, 209, 255)
      draw_gaze(painter, gaze, color_rgba)
      
      # Drawing all previously painted gazes of failed target holding
      for gaze_qpoint, color_rgba in gaze_failure_store:
        draw_gaze(painter, gaze_qpoint, color_rgba)

      # Drawing all previously painted gazes of successful target holding
      for gaze_qpoint, color_rgba in gaze_success_store:
        draw_gaze(painter, gaze_qpoint, color_rgba)

  # Set the renderer function to the widget's renderer
  widget.renderer = renderer
  # Store the context information into .tha file
  await context.log(json.dumps(context.config))
  # await context.log(json.dumps(context.task_config.unwrap()))

  # This is the ACQUIRE_FIXATION state that was initiated above "state = State.ACQUIRE_FIXATION"
  print(state)
  acquired = False
  while not acquired:
    #print(state)
    # Wait for the gaze to be within the fixation window
    acquired = await wait_for(context, lambda: QPoint.dotProduct(gaze - center, gaze-center)**.5 < accptolerance_pix, timedelta(seconds=cum_blinking_ms))

  state = State.FIXATE1
  await context.log('BehavState=FIXATE1_post-drawing') # saving any variables / data from code
  print(state)
  print("xxxxxxxxxxxxxxxxxxxxxxxxxconfig['name']xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
  print("xxxxxxxxxxxxxxxxxxxxxxxxxconfig['name']xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
  print(config['name'])
  print("xxxxxxxxxxxxxxxxxxxxxxxxxconfig['name']xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
  print("xxxxxxxxxxxxxxxxxxxxxxxxxconfig['name']xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
  widget.update()
  # Wait for the gaze to hold within the fixation window for the fix1 duration
  await wait_for_hold(context, lambda: QPoint.dotProduct(gaze - center, gaze-center)**.5 < accptolerance_pix, timedelta(seconds=fix1_duration), timedelta(seconds=cum_blinking_ms))
  await context.log(f"Gaze[X,Y]_pix-abs_after-FIXATE1={gaze}")
  await context.log(f"Gaze[X,Y]_deg-abs_after-FIXATE1={converter.relpix_to_absdeg(gaze.x(), gaze.y())}")

  state = State.TARGET_PRESENTATION
  await context.log('BehavState=TARGET_PRESENTATION_post-drawing') # saving any variables / data from code
  print(state)
  widget.update()
  # Wait for the gaze to hold within fixation cross tolerances for the target presentation duration
  success = await wait_for_hold(context, lambda: QPoint.dotProduct(gaze - center, gaze-center)**.5 < accptolerance_pix, timedelta(seconds=target_present_dur), timedelta(seconds=cum_blinking_ms))

  if not success:
    await context.log('TrialResult=FAILURE') # saving any variables / data from code
    state = State.FAILURE
    print(state)
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  state = State.FIXATE2
  await context.log('BehavState=FIXATE2') # saving any variables / data from code
  print(state)
  widget.update()
  # Wait for the gaze to hold within the fixation window for the fix2 duration
  success = await wait_for_hold(context, lambda: QPoint.dotProduct(gaze - center, gaze-center)**.5 < accptolerance_pix, timedelta(seconds=fix2_duration), timedelta(seconds=cum_blinking_ms))

  if not success:
    await context.log('TrialResult=FAILURE') # saving any variables / data from code
    state = State.FAILURE
    print(state)
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  state = State.ACQUIRE_TARGET
  await context.log('BehavState=ACQUIRE_TARGET_start') # saving any variables / data from code
  print(state)
  widget.update()
  # Wait for the gaze to move to the target position within the decision timeout
  success = await wait_for(context, lambda: QPoint.dotProduct(gaze - targetpos_pix, gaze-targetpos_pix)**.5 < accptolerance_pix, timedelta(seconds=decision_timeout))
  await context.log(f"Gaze[X,Y]_pix-abs_after-acquiring-target={gaze}")
  await context.log(f"Gaze[X,Y]_deg-abs_after-acquiring-target={converter.relpix_to_absdeg(gaze.x(), gaze.y())}")

  if not success:
    gaze_failure_store.append((gaze, QColor(255, 69, 0, 128)))
    await context.log('TrialResult=FAILURE') # saving any variables / data from code
    state = State.FAILURE
    print(state)
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  state = State.HOLD_TARGET
  await context.log('BehavState=HOLD_TARGET_start') # saving any variables / data from code
  print(state)
  widget.update()
  # Wait for the gaze to hold on the target position for the fix2 timeout
  success = await wait_for_hold(context, lambda: QPoint.dotProduct(gaze - targetpos_pix, gaze-targetpos_pix)**.5 < accptolerance_pix,
                                timedelta(seconds=targethold_duration), timedelta(seconds=cum_blinking_ms))
  await context.log(f"Gaze[X,Y]_pix-abs_after-holding-target={gaze}")
  await context.log(f"Gaze[X,Y]_deg-abs_after-holding-target={converter.relpix_to_absdeg(gaze.x(), gaze.y())}")

  if not success:
    gaze_failure_store.append((gaze, QColor(255, 69, 0, 128)))
    await context.log('TrialResult=FAILURE') # saving any variables / data from code
    state = State.FAILURE
    print(state)
    # widget.update() # DC added
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  gaze_success_store.append((gaze, QColor(0, 255, 0, 128)))
  await context.log('TrialResult=SUCCESS') # saving any variables / data from code
  state = State.SUCCESS
  print(state)
  # widget.update() # DC added
  success_sound.play()
  await context.sleep(timedelta(seconds=1)) # 1s delay to allow playing the sound; sound doesn't play without this delay
  if config['name'] == "photic":
    trial_success_count += 1
  # "TaskResult" is used to determine whether the trial is or is not removed from the queue
  # If TaskResult(False), the trial is not removed from the queue. If TaskResult(True), the trial is removed from the queue.
  return TaskResult(False)


