"""
Implementation of the Center fixation saccade task v1.0 (2025/04/29)
"""
import time
import typing
import numbers
import logging
from datetime import timedelta
import random
import numpy as np # import Numpy to draw Gaussian

from ..qt import *
from PyQt5.QtCore import Qt

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol, animate, CanvasProtocol, create_task_with_exc_handling, RenderOutput
from .. import task_controller_pb2
from ..thalamus_pb2 import AnalogResponse, Span
from ..config import *

LOGGER = logging.getLogger(__name__)

shapes = ['rectangle', 'gaussian', 'square'] # Define the possible shapes

# Define the framerate and frame interval for the task
FRAMERATE = 60
INTERVAL = 1/FRAMERATE

converter = None
center = None
center_f = None
num_circles = 7 # The last 2 circles usually end up being too large for the screen height, hence the actual # = num_circles - 2
circle_radii = []
rand_pos_i = 0
trial_num = 0
trial_photic_count = 0
trial_photic_success_count = 0
reward_total_released_ms = 0
trial_catch_count = 0
trial_catch_success_count = 0
drawn_objects = []
rand_pos = []
gaze_success_store = []
gaze_failure_store = []

WATCHING = False

# === Utility Functions ===
def gaze_valid(gaze: QPoint, monitorsubj_W_pix: int, monitorsubj_H_pix: int) -> QPoint:
    """
    A function to check and change if needed the current gaze value.
    """
    if gaze.x() < 0 or gaze.x() > monitorsubj_W_pix or gaze.y() < 0 or gaze.y() > monitorsubj_H_pix:
      return QPoint(99999, 99999)
    else:
      return gaze

# === Data Structures ===
class Size(typing.NamedTuple):
  """Screen size in pixels."""
  width: int
  height: int

class Converter:
  """Handles conversions between degrees and pixels."""
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

# === State Definitions ===
class State(enum.Enum):
  """Enumeration for task states."""
  ACQUIRE_FIXATION = enum.auto()
  FIXATE = enum.auto()
  SUCCESS = enum.auto()
  FAILURE = enum.auto()
  ABORT = enum.auto()

# === GUI Widgets ===
def create_widget(task_config: ObservableCollection) -> QWidget:
  """Creates the main configuration widget."""
  result = QWidget()
  layout = QVBoxLayout()
  result.setLayout(layout)

  """
  Below: We're building a Form (widgets.py) object that will use task_config to initialize
  the parameters of this task. Values are taken from the provided "task_config" argument, and
  if the key (e.g. decision_timeout) is not found in the task_config, the parameters will
  default to the values provided below. The build function also wires up all the
  listeners to update the task_config when changes are made.
  By posting the config as a message into stored file, all latest config values will be saved.
  The exception is the "Uniform" variables, for which config stores "min" and "max" values.
  The "Uniform" value used in a given trial is added to config when calling "get_value()" method.
  If chosing a value for "Uniform" variable without using the "get_value()", coder needs to ensure
  that context.trial_summary_data.used_values['variable_name'] is set to the value used in the trial.
  """
  form = Form.build(task_config, ["Name:", "Min:", "Max:"],
    Form.Uniform('\u23F0 Duration of fixation to get reward', 'fix_dur_to_get_reward_ms', 1000, 2000, 'ms'),
    Form.Uniform('\u23F0 Penalty Delay', 'penalty_delay', 3000, 3000, 'ms'),
    Form.Constant('\u23F0 Max allowed single blink duration', 'blink_dur_ms', 500, 'ms'),                    
    Form.Uniform('\U0001F4A7 Reward per trial', 'reward_pertrial_ms', 10, 350, 'ms'),
    Form.Constant('\U0001F4A7 Reward starting volume', 'reward_start_ml', 500, 'mL'),
    Form.Constant('\U0001F4A7 Reward release rate', 'reward_rate_mlps', 1000, 'mL/s'),
    Form.Constant('Number of checkers', 'number_of_checkers', 4, ''),
    Form.Constant('Checker size (deg)', 'checker_size_deg', 0.5, '°'),
    Form.Constant('Checkerboard toggle rate (Hz)', 'rate_of_checkerboard_hz', 5, 'Hz'),
    Form.Constant('Checkerboard duty cycle (%)', 'duty_cycle_checkerboard_per', 75, '%'),
    Form.Constant('Checkerboard step size (deg)', 'step_size_rel_deg', 1, '°'),
    Form.Constant('Checkerboard left edge (deg)', 'left_edge_rel_deg', -5, '°'),
    Form.Constant('Checkerboard right edge (deg)', 'right_edge_rel_deg', 25, '°'),
    Form.Constant('Checkerboard top edge (deg)', 'top_edge_rel_deg', 25, '°'),
    Form.Constant('Checkerboard bottom edge (deg)', 'bottom_edge_rel_deg', -25, '°'),
    Form.Constant('Repeats per location', 'repeats_per_location', 3, ''),
    Form.Constant('Checkerboard FPS', 'checkerboard_fps', 40, 'Hz'),
    Form.Constant('Gaze acceptance diameter (0.1-4.0)', 'accptolerance_deg', 2, '\u00B0'), # Define the diameter in degrees of the area where gaze is accepted as being correct
    Form.Constant('\u2300 Diameter of green feedback circle', 'diameter_of_green_feedback_circle_deg', 4, '\u00B0'), # Define the diameter in degrees of the area where gaze is accepted as being correct
    Form.Constant('\U0001F5A5 Subject\'s distance to the screen', 'monitorsubj_dist_m', .57, 'm'),
    Form.Constant('\U0001F5A5 Subject monitor\'s width', 'monitorsubj_width_m', .5283, 'm'),
    Form.Constant('\U0001F5A5 Subject monitor\'s width', 'monitorsubj_W_pix', 1920, 'pix'),
    Form.Constant('\U0001F5A5 Subject monitor\'s height', 'monitorsubj_H_pix', 1080, 'pix'),
    Form.Constant('\U0001F5A5 Subject monitor\'s physical brightness setting', 'monitorsubj_brightness_perc', 100, '%'),
    Form.Constant('\U0001F5A5 Operator monitor\'s width', 'monitoroper_W_pix', 1920, 'pix'),
    Form.Constant('\U0001F5A5 Operator monitor\'s height', 'monitoroper_H_pix', 1200, 'pix'),
    Form.String('\U0001F5A5 Subject monitor\'s model', 'monitorsubj_model', 'LG24GQ50B-B'),
    Form.String('\U0001F5A5 Operator monitor\'s model', 'monitoroper_model', 'DELLU2412M'),
    Form.Color('Target Color', 'target_color', QColor(255, 255, 255)),
    Form.Color('Background Color', 'background_color', QColor(128, 128, 128, 255)),
    Form.Choice('Shape', 'shape', list(zip(shapes, shapes))),  # Add the shape attribute
  )

  # # Add a button to show/hide rarely changed parameters
  # def on_click():
  #   if form.isVisible():
  #     form.setVisible(False)
  #   else:
  #     form.setVisible(True)
  # button = QPushButton('Show/Hide')
  # layout.addWidget(button)
  # button.clicked.connect(on_click)

  layout.addWidget(form)

  # spinbox allows to constraint value options for above constants
  monitorsubj_W_pix_spinbox = form.findChild(QDoubleSpinBox, "monitorsubj_W_pix")
  monitorsubj_W_pix_spinbox.setRange(100, 4000)
  monitorsubj_W_pix_spinbox.setSingleStep(10)
  monitorsubj_H_pix_spinbox = form.findChild(QDoubleSpinBox, "monitorsubj_H_pix")
  monitorsubj_H_pix_spinbox.setRange(100, 4000)
  monitorsubj_H_pix_spinbox.setSingleStep(10)
  reward_pertrial_ms_min_spinbox = form.findChild(QDoubleSpinBox, "reward_pertrial_ms_min")
  reward_pertrial_ms_min_spinbox.setRange(10, 500)
  reward_pertrial_ms_min_spinbox.setSingleStep(1)
  reward_pertrial_ms_max_spinbox = form.findChild(QDoubleSpinBox, "reward_pertrial_ms_max")
  reward_pertrial_ms_max_spinbox.setRange(100, 500)
  reward_pertrial_ms_max_spinbox.setSingleStep(1)
  reward_rate_mlps_spinbox = form.findChild(QDoubleSpinBox, "reward_rate_mlps")
  reward_rate_mlps_spinbox.setRange(10, 500)
  reward_rate_mlps_spinbox.setSingleStep(1)
  accptolerance_deg_spinbox = form.findChild(QDoubleSpinBox, "accptolerance_deg")
  accptolerance_deg_spinbox.setRange(.1, 60.0)
  accptolerance_deg_spinbox.setSingleStep(0.1)

  return result

# === State Handlers (low-level, reusable) ===
async def acquire_fixation_func(context, get_gaze, center, accptolerance_pix, monitorsubj_W_pix, monitorsubj_H_pix):
    reaquire_dur_s = 999999 # a very long duration to avoid passing ACQUIRE_FIXATION before acquiring the fixation cross
    while True:
        acquired = await wait_for(
            context,
            lambda: QPoint.dotProduct(
                gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
                gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
            ) ** .5 < accptolerance_pix,
            timedelta(seconds=reaquire_dur_s)
        )
        if acquired:
            return

async def fixate_func(context, get_gaze, center, accptolerance_pix, duration1, duration2, monitorsubj_W_pix, monitorsubj_H_pix):
  # Wait for the gaze to hold within the fixation window for the fix2 duration
  success = await wait_for_hold(
      context,
      lambda: QPoint.dotProduct(
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
      ) ** .5 < accptolerance_pix,
      timedelta(seconds=duration1), # target presentation duration
      timedelta(seconds=duration2) # allowed single blink duration
  )
  return success

# === State Phase Handlers (high-level, orchestration) ===
async def handle_acquire_fixation(
    context,
    get_gaze,
    center,
    accptolerance_pix,
    monitorsubj_W_pix,
    monitorsubj_H_pix
    ):
    global state
    state = State.ACQUIRE_FIXATION
    await context.log('BehavState=ACQUIRE_FIXATION_post-drawing')
    print(state)
    # widget.update()
    await acquire_fixation_func(
        context,
        get_gaze,
        center,
        accptolerance_pix,
        monitorsubj_W_pix,
        monitorsubj_H_pix
    )
    return

async def handle_fixate(
    context,
    get_gaze,
    center,
    accptolerance_pix,
    fix_dur_to_get_reward_ms,
    blink_dur_ms,
    monitorsubj_W_pix,
    monitorsubj_H_pix,
    widget
):
    global state
    state = State.FIXATE
    await context.log('BehavState=FIXATE_post-drawing')
    print(state)
    widget.update()
    success = await fixate_func(context, get_gaze, center, accptolerance_pix, fix_dur_to_get_reward_ms, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix)
    return success

# === Main Task Logic ===
@animate(60) # This line allows the gaze to be sampled at 60 FPS this also allows painting 
# the gaze position in the Operator View continuously (i.e. continuously calling "widget.update()")
# without this line we would need to manually update the widget ("widget.update()") 
# to update the gaze position. "renderer()" will continue running indefinitely until the task is stopped
# or animation is stopped via QTimer() or condition. Calling "widget.update()" again
# rechecks current state and repaints widget based on current state without ever stopping the animation.
# Define an asynchronous function to run the task with a 60 FPS animation
async def run(context: TaskContextProtocol) -> TaskResult: #pylint: disable=too-many-statements
  """Main entry point for the Gaussian delayed saccade task."""
  global converter, center, center_f, num_circles, circle_radii, rand_pos_i, \
        trial_num, trial_photic_count, trial_photic_success_count, trial_catch_count, \
        trial_catch_success_count, drawn_objects, rand_pos, reward_total_released_ms, \
        gaze_success_store, gaze_failure_store, \
        photodiode_blinking_square, photodiode_static_square, WATCHING, state

  # Get the task configuration
  config = context.task_config
  monitorsubj_W_pix = config['monitorsubj_W_pix']
  monitorsubj_H_pix = config['monitorsubj_H_pix']
  monitorsubj_dist_m = config['monitorsubj_dist_m']
  monitorsubj_width_m = config['monitorsubj_width_m']


  if converter is None:    
    # If you turn on saving only after running >=1 trial, then 
    # converter = Converter(Size(1920, 1080), .5283, .57)
    converter = Converter(Size(monitorsubj_W_pix, monitorsubj_H_pix), monitorsubj_width_m, monitorsubj_dist_m)
    center = QPoint(int(converter.screen_pixels.width), int(converter.screen_pixels.height))/2
    center_f = QPointF(float(converter.screen_pixels.width), float(converter.screen_pixels.height))/2
    photodiode_blinking_square = QColor(255, 255, 255, 255) # Create a QColor object with white color and transparency control (i.e. alpha)
    photodiode_static_square = QColor(0, 0, 0, 255)
   
    def on_change(source, action, key, value):
      if not isinstance(source, ObservableDict):
        return
      
      if not source.get('type', None) == 'STORAGE':
        return
      
      if key == 'Running' and value: # do smthg when STORAGE gets switched on
        context.trial_summary_data.used_values['photodiode_blinking_square_color_rgba'] = [photodiode_blinking_square.red(), \
            photodiode_blinking_square.green(), photodiode_blinking_square.blue(), photodiode_blinking_square.alpha()]
        context.trial_summary_data.used_values['photodiode_static_square_color_rgba'] = [photodiode_static_square.red(), \
            photodiode_static_square.green(), photodiode_static_square.blue(), photodiode_static_square.alpha()]   
        context.trial_summary_data.used_values['code_name_and_version'] = ["center_fixation_task_code_v1.0"]

    if not WATCHING:
      context.config.add_recursive_observer(on_change) # this method is responsible for registering 
      # "on_change()" as a listener for changes. "on_change" will be executed automatically by the
      # add_recursive_observer() mechanism whenever the relevant event occurs
      WATCHING = True
 

  """
  Below is an object that contains a realization generated by sampling from the random
  distributions defined in the task_config. It itself has no logic, it simply holds
  the realization's values.
  """

  # Get the checkerboard configuration from the task config
  number_of_checkers = config['number_of_checkers']
  checker_size_deg = config['checker_size_deg']
  checker_size_pix = int(converter.deg_to_pixel_rel(checker_size_deg))
  center_x = int(converter.screen_pixels.width / 2)
  center_y = int(converter.screen_pixels.height / 2)

  # Load the sounds for failure, abort, and success
  current_directory = os.getcwd() # Get the current working directory
  # Define a relative path (e.g., accessing a file in a subdirectory)
  relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'failure_clip.wav')
  failure_sound = QSound(relative_path) # Load the .wav file (replace with your file path)
  relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'failure_clip.wav')
  # Sound file is called "failure" because in default tasks it's used for FAILURE, in VCP tasks it's used for ABORT
  abort_sound = QSound(relative_path) 
  relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'success_clip.wav')
  success_sound = QSound(relative_path) 

  # Define the vertices for the fixation cross in degrees
  vertices_deg = [ 
      (-1, 0), (1, 0),  # Horizontal line
      (0, -1), (0, 1)  # Vertical line
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
  target_color_rgb = config['target_color']
  background_color = config['background_color']
  background_color_qt = QColor(background_color[0], background_color[1], background_color[2], 255)
  
  # Get various timeouts from the context (user GUI)
  fix_dur_to_get_reward_ms = context.get_value('fix_dur_to_get_reward_ms') / 1000
  penalty_delay = context.get_value('penalty_delay') / 1000
  blink_dur_ms = context.get_value('blink_dur_ms') / 1000

  def pick_random_value(min_val, max_val, step):
    # Generate the range of possible values using numpy
    possible_values = np.arange(min_val, max_val + step, step).tolist()
    # Pick a random value from the possible values
    return random.choice(possible_values)

  # Create a Gaussian gradient for the target with randomly selected luminance, orientation, size and location
  # Random selection is based on user-defined range min...max and step size
  reward_pertrial_ms = context.get_value('reward_pertrial_ms') # return a uniform random number
  reward_rate_mlps = config['reward_rate_mlps']
  reward_start_ml = config['reward_start_ml']
  radius_of_green_feedback_circle_pix = converter.deg_to_pixel_rel(config['diameter_of_green_feedback_circle_deg']) / 2

  def draw_checkerboard(
      painter: QPainter,
      center_x: int,
      center_y: int,
      number_of_checkers: int,
      checker_size_pix: int
  ):
      # Calculate top-left corner
      board_size = number_of_checkers * checker_size_pix
      x0 = center_x - board_size // 2
      y0 = center_y - board_size // 2
      for i in range(number_of_checkers):
          for j in range(number_of_checkers):
              color = QColor(255, 255, 255) if (i + j) % 2 == 0 else QColor(0, 0, 0)
              painter.fillRect(
                  x0 + j * checker_size_pix,
                  y0 + i * checker_size_pix,
                  checker_size_pix,
                  checker_size_pix,
                  color
              )

  def generate_checkerboard_locations(converter, config):
      """
      Generate a shuffled list of (x, y) pixel offsets for checkerboard locations.
      """
      step_size_rel_deg = config['step_size_rel_deg']
      left_edge_rel_deg = config['left_edge_rel_deg']
      right_edge_rel_deg = config['right_edge_rel_deg']
      top_edge_rel_deg = config['top_edge_rel_deg']
      bottom_edge_rel_deg = config['bottom_edge_rel_deg']

      # Convert degrees to pixels
      step_size_rel_pix = int(converter.deg_to_pixel_rel(step_size_rel_deg))
      left_edge_rel_pix = int(converter.deg_to_pixel_rel(left_edge_rel_deg))
      right_edge_rel_pix = int(converter.deg_to_pixel_rel(right_edge_rel_deg))
      top_edge_rel_pix = int(converter.deg_to_pixel_rel(top_edge_rel_deg))
      bottom_edge_rel_pix = int(converter.deg_to_pixel_rel(bottom_edge_rel_deg))

      # Generate grid of locations
      x_positions = np.arange(left_edge_rel_pix, right_edge_rel_pix + 1, step_size_rel_pix)
      y_positions = np.arange(bottom_edge_rel_pix, top_edge_rel_pix + 1, step_size_rel_pix)
      checkerboard_location_pix = [(int(x), int(y)) for x in x_positions for y in y_positions]
      random.shuffle(checkerboard_location_pix)
      return checkerboard_location_pix  

  def drawText(painter, text, location: QPoint, background_color_qt: QColor):
    painter.save()  # Save the current state of the painter
    painter.setClipRect(painter.viewport())  # Set the clip region to the current viewport
    if background_color_qt.red() == 0 and background_color_qt.green() == 0 and background_color_qt.blue() == 0: # if black background
      painter.setPen(QColor(255, 255, 255))
    else:
      painter.setPen(QColor(0, 0, 0))
    painter.setFont(QFont('Arial', 20))
    rect = painter.viewport()  # Get the current viewport rectangle
    rect.moveTopLeft(location)  # Move the top-left corner of the rectangle to the specified location
    rect.setHeight(50)  # Set the height of the rectangle to 50 pixels
    painter.drawText(rect, Qt.AlignLeft | Qt.AlignTop, text)  # Draw the text within the rectangle
    painter.restore()  # Restore the painter to its previous state

  def draw_gaze(painter, gaze_qpoint, color_rgba):
    path = QPainterPath()
    gaze_f = QPointF(gaze_qpoint)
    path.addEllipse(gaze_f, 12, 12)
    painter.fillPath(path, color_rgba) 

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

  # Generate all checkerboard locations for this session
  checkerboard_locations_pix = generate_checkerboard_locations(converter, config)
  repeats_per_location = config['repeats_per_location']
  random.shuffle(checkerboard_locations_pix)  # Shuffle all locations for the session

  # Use an iterator to cycle through locations
  location_iter = iter(checkerboard_locations_pix)

  def get_next_location():
      nonlocal location_iter, checkerboard_locations_pix
      try:
          return next(location_iter)
      except StopIteration:
          # All locations exhausted, reshuffle and restart
          random.shuffle(checkerboard_locations_pix)
          location_iter = iter(checkerboard_locations_pix)
          return next(location_iter)
      
  def renderer(painter: QPainter):
    global state
    painter.fillRect(QRect(0, 0, 4000, 4000), background_color_qt) # QColor(128, 128, 128, 255); make the background of desired color
    painter.fillRect(int(widget.width() - 150), int(widget.height() - 150), 150, 150, photodiode_static_square) # background small square bottom-right

    # Clearing Operator View canvas
    if getattr(context.widget, 'do_clear', False):
      print('DO CLEAR')
      context.widget.do_clear = False

    # Draw the fixation cross and Gaussian based on the current state
    if state in (State.ACQUIRE_FIXATION, State.FIXATE):
      pen = painter.pen()
      pen.setWidth(4)
      pen.setColor(QColor(255, 0, 0))
      painter.setPen(pen)
      painter.drawPath(cross)
      center_x = int(converter.screen_pixels.width / 2)
      center_y = int(converter.screen_pixels.height / 2)


      if state == State.FIXATE: # draw green circle around the fixation cross as feedback
        pen = painter.pen()
        pen.setWidth(4)
        pen.setColor(QColor(0, 255, 0))
        painter.setPen(pen)
        painter.drawEllipse(QPointF(center_x, center_y), radius_of_green_feedback_circle_pix, radius_of_green_feedback_circle_pix) # the last 2 inputs = radii of the elipse
        
        checkerboard_x_pix = int(converter.screen_pixels.width / 2) + checkerboard_locations_pix[0][0]
        checkerboard_y_pix = int(converter.screen_pixels.height / 2) + checkerboard_locations_pix[0][1]  
        draw_checkerboard(
            painter,
            checkerboard_x_pix,
            checkerboard_y_pix,
            number_of_checkers,
            checker_size_pix
          )
    
    elif state == State.SUCCESS:
      pen = painter.pen()
      pen.setWidth(4)
      pen.setColor(QColor(255, 0, 0))
      painter.setPen(pen)
      painter.drawPath(cross)
      painter.fillRect(int(widget.width() - 100), int(widget.height()-100), 100, 100, photodiode_blinking_square) # photodiode white square presentation

    # Draw only in the OPERATOR view
    with painter.masked(RenderOutput.OPERATOR): # using a context manager to temporarily change the drawing behavior of the painter object
      # A feature of the operator view is that you can draw stuff only for the operator into it. Anything in this 
      # with painter.masked(RenderOutput.OPERATOR) block will only appear in the operator view.
      
      path = QPainterPath()
      path.addEllipse(center_f, accptolerance_pix, accptolerance_pix)
      painter.fillPath(path, QColor(255, 255, 255, 128))

      # Drawing the gaze position as a continuously moving point
      color_rgba = QColor(138, 43, 226, 255) # purple
      draw_gaze(painter, gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), color_rgba)

      # Drawing text message on the operator view
      drawText(painter, str(state), QPoint(0, 30), background_color_qt) # Draw the text message
      drawText(painter, f"TRIAL_NUM={trial_num}", QPoint(0, 60), background_color_qt) # Draw the text message
      drawText(painter, f"Reward = {round(reward_total_released_ms * reward_rate_mlps / 1000)}mL / {reward_start_ml}mL", QPoint(0, 90), background_color_qt) # Draw the text message
      temp_gaze = gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix)
      drawn_text = f"({temp_gaze.x()}, {temp_gaze.y()})"
      drawText(painter, drawn_text, temp_gaze, background_color_qt) # Draw the text message
    
      # Drawing all previously painted gazes of failed target holding
      # for gaze_qpoint, color_rgba in gaze_failure_store:
      #   draw_gaze(painter, gaze_qpoint, color_rgba)

      # # Drawing all previously painted gazes of successful target holding
      # for gaze_qpoint, color_rgba in gaze_success_store:
      #   draw_gaze(painter, gaze_qpoint, color_rgba)

  # Set the renderer function to the widget's renderer
  widget.renderer = renderer
  # Store the context information into .tha file
  await context.log(json.dumps(context.config)) 

  # trial counters before the 1st "return" statement
  trial_num += 1 # Increment the trial counter
  await context.log(f"TRIAL_NUM={trial_num}") # saving any variables / data from code
  print(f"Started trial # {trial_num}") # print to console

  # Handles State.ACQUIRE_FIXATION
  await handle_acquire_fixation(context, lambda: gaze, center, accptolerance_pix, monitorsubj_W_pix, monitorsubj_H_pix)

  # Handles State.FIXATE
  success = await handle_fixate(context, lambda: gaze, center, accptolerance_pix, fix_dur_to_get_reward_ms, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix, widget)

  if not success:
    gaze_failure_store.append((gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), QColor(255, 69, 0, 128)))
    await context.log('TrialResult=ABORT') # saving any variables / data from code
    state = State.ABORT
    print(state)
    abort_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  gaze_success_store.append((gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), QColor(0, 255, 0, 128)))
  await context.log('TrialResult=SUCCESS') # saving any variables / data from code
  state = State.SUCCESS
  print(state)
  # widget.update() # DC added
  success_sound.play()
  await context.sleep(timedelta(seconds=1)) # 1s delay to allow playing the sound; sound doesn't play without this delay
  
  # RELEASING REWARD
  # reward_pertrial_ms = int(context.get_reward(0)) # define the channel (aka column) # to be read from the csv file 
  reward_total_released_ms += reward_pertrial_ms
  await context.log("starting_reward_release_of = %d ms, total_released = %d ms"%(int(reward_pertrial_ms), \
                                                                        int(reward_total_released_ms)) )
  create_task_with_exc_handling(context.inject_analog('reward_in', AnalogResponse(
    data=[5,0], # 5 = HIGH, 0 = LOW voltages
    spans=[Span(begin=0,end=2,name='Reward')], 
    sample_intervals=[1_000_000*int(reward_pertrial_ms)]) # multiplyin by 1_000_000 will give us nanoseconds (ns)
  ))

  # "TaskResult" is used to determine whether the trial is or is not removed from the queue
  # If TaskResult(False), the trial is not removed from the queue. If TaskResult(True), the trial is removed from the queue.
  return TaskResult(False)


