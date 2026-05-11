"""
Implementation of the Center fixation saccade task v1.0 (2025/04/29)
"""
import time
import typing
import numbers
import logging
from datetime import datetime, timedelta
import random
import numpy as np # import Numpy to draw Gaussian

from ..qt import *
from PyQt6.QtCore import Qt, pyqtSignal

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol, animate, CanvasProtocol, create_task_with_exc_handling, RenderOutput
from .. import task_controller_pb2
from ..thalamus_pb2 import AnalogResponse, Span
from ..config import *

LOGGER = logging.getLogger(__name__)

# Define the framerate and frame interval for the task
FRAMERATE = 60
INTERVAL = 1/FRAMERATE

converter = None
center = None
center_f = None
rand_pos_i = 0
trial_count = 0
success_count = 0
abort_count = 0
reward_count = 0
reward_total_released_ms = 0
gaze_success_store = []
gaze_failure_store = []
location_iter = None
checkerboard_locations_pix = None
repetition = 1
abort = 0
current_location = [0,0]
abort_location = [0,0]

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

def start_recording():
  """
  Called when user presses START RECORDING
  """
  # print("START RECORDING:", cfg)
  print("START RECORDING")

  # intan.configure(cfg)
  # intan.start()

def stop_recording():
  """
  Called when user presses STOP RECORDING
  """
  global location_iter, converter 
  # print("STOP RECORDING:", cfg)
  print("STOP RECORDING")

  # intan.configure(cfg)
  # intan.start()


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
    # Convert degrees to absolute pixel coordinates (absolute means that the top-left corner of the screen is [0, 0])
    result = self.deg_to_pixel_rel(*args)
    # Coordinates are relative to the center of the screen, so add the screen center to get absolute coordinates
    if len(result) == 2:
      return result[0] + self.screen_pixels.width/2, result[1] + self.screen_pixels.height/2,
    else:
      return result[0] + self.screen_pixels.width/2

  def deg_to_pixel_rel(self, *args) -> typing.Tuple[int, int]:
    # Convert degrees to relative pixel coordinates (relative means that center of the screen is [0, 0])
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
  INITIATE = enum.auto()
  FIXATE = enum.auto()
  SUCCESS = enum.auto()
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
    Form.Constant('\u23F0 Duration of initial fixation', 'init_duration', 500, 'ms'),
    Form.Constant('\u23F0 Checkerboard presentation duration', 'presentation_duration', 250, 'ms'),
    Form.Constant('\u23F0 Inter-checkerboard duration', 'rest_duration', 250, 'ms'),
    Form.Constant('\u23F0 Penalty Delay', 'penalty_delay', 3000, 'ms'),
    Form.Constant('\u23F0 Max allowed single blink duration', 'blink_duration', 1, 'ms'),
    Form.Uniform('\U0001F4A7 Reward frequency - every', 'reward_frequency_s', 2, 3, 's'),
    Form.Uniform('\U0001F4A7 Reward amount', 'reward_ms', 100, 200, 'ms'),
    Form.Constant('\U0001F5FA Fixation cross\' x coordinate', 'cross_x_pos', 0, ''), # center of the screen is [0,0] deg
    Form.Constant('\U0001F5FA Fixation cross\' y coordinate', 'cross_y_pos', 0, ''), # center of the screen is [0,0] deg
    Form.Constant('\U0001F522 Number of squares along each edge', 'number_of_squares', 4, ''),
    Form.Constant('\u2195\u2194 Square size', 'square_size', 1, '°'),
    Form.Constant('\U0001F522 Repeats per location', 'repeats_per_location', 3, ''),
    Form.Constant('\U0001F97E Checkerboard step size', 'step_size_deg', 2, '°'),
    Form.Constant('\U0001F5FA Checkerboard left edge', 'left_edge_deg', -6, '°'),
    Form.Constant('\U0001F5FA Checkerboard right edge', 'right_edge_deg', 1, '°'),
    Form.Constant('\U0001F5FA Checkerboard top edge', 'top_edge_deg', 6, '°'),
    Form.Constant('\U0001F5FA Checkerboard bottom edge', 'bottom_edge_deg', 0, '°'),
    Form.Constant('\u25EF Radius for gaze acceptance', 'accpt_gaze_radius_deg', 2, '°'), # Define the radius in degrees of the area where gaze is accepted as being correct
    Form.Constant('\U0001F5A5 Subject\'s distance to the screen', 'monitorsubj_dist_m', .57, 'm'),
    Form.Constant('\U0001F5A5 Subject monitor\'s width', 'monitorsubj_width_m', .5283, 'm'),
    Form.Constant('\U0001F5A5 Subject monitor\'s width', 'monitorsubj_W_pix', 1920, 'pix'),
    Form.Constant('\U0001F5A5 Subject monitor\'s height', 'monitorsubj_H_pix', 1080, 'pix'),
    Form.Constant('\U0001F5A5 Subject monitor\'s physical brightness setting', 'monitorsubj_brightness_perc', 100, '%'),
    Form.Constant('\U0001F5A5 Operator monitor\'s width', 'monitoroper_W_pix', 1920, 'pix'),
    Form.Constant('\U0001F5A5 Operator monitor\'s height', 'monitoroper_H_pix', 1200, 'pix'),
    Form.String('\U0001F5A5 Subject monitor\'s model', 'monitorsubj_model', 'LG24GQ50B-B'),
    Form.String('\U0001F5A5 Operator monitor\'s model', 'monitoroper_model', 'DELLU2412M'),
    Form.Color('Background Color', 'background_color', QColor(31, 31, 31, 255))
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

  recording_widget = RecordingWidget(task_config)
  layout.addWidget(recording_widget)

  recording_widget.recordingStartRequested.connect(start_recording)
  recording_widget.recordingStopRequested.connect(stop_recording)

  # spinbox allows to constraint value options for above constants
  # reward_ms_min_spinbox = form.findChild(QDoubleSpinBox, "reward_ms_min")
  # reward_ms_min_spinbox.setRange(0, 500)
  # reward_ms_min_spinbox.setSingleStep(1)
  # reward_ms_max_spinbox = form.findChild(QDoubleSpinBox, "reward_ms_max")
  # reward_ms_max_spinbox.setRange(0, 500)
  # reward_ms_max_spinbox.setSingleStep(1)
  # accpt_gaze_radius_deg_spinbox = form.findChild(QDoubleSpinBox, "accpt_gaze_radius_deg")
  # accpt_gaze_radius_deg_spinbox.setRange(.1, 60.0)
  # accpt_gaze_radius_deg_spinbox.setSingleStep(0.1)
  
  w_label = form.findChild(QLabel, "monitorsubj_W_pix_label")
  w_label_original = w_label.text()
  i = 0

  def on_change(source, action, key, value):
    nonlocal i
    if key in ('monitorsubj_W_pix', 'monitorsubj_H_pix'): # If the monitor size is changed, then update the value in degrees
      # add the rest of the 3 monitor dimensions
      converter0 = Converter(Size(task_config['monitorsubj_W_pix'], task_config['monitorsubj_H_pix']), \
                             task_config['monitorsubj_width_m'], task_config['monitorsubj_dist_m']) # replace variables with GUI inputs!!
      # i += 1 # Replace i with conversion function into degrees
      i = converter0.relpix_to_absdeg(task_config['monitorsubj_H_pix'], task_config['monitorsubj_W_pix'])
      w_label.setText(w_label_original + f' ({round(i[1], 2)} deg)') # add (... deg) to the label
      converter0 = None # destroy the converter just in case
 
  task_config.add_recursive_observer(on_change, lambda: isdeleted(result), True)
  return result

# === Recording Widget ===
class RecordingWidget(QWidget):
    recordingStartRequested = pyqtSignal(dict)
    recordingStopRequested = pyqtSignal()

    def __init__(self, config: typing.MutableMapping, parent=None, show_waveform: bool = False):
        super().__init__(parent)
        self.config = config
        self.is_recording = False

        # ---- layout ----
        root = QGridLayout(self)

        # ---- Start / Stop buttons ----
        buttons_widget = QWidget(self)
        buttons_layout = QHBoxLayout(buttons_widget)
        buttons_layout.setContentsMargins(0, 0, 0, 0)

        self.start_btn = QPushButton("Start record")
        self.stop_btn = QPushButton("Stop record")
        self.stop_btn.setEnabled(False)

        self.start_btn.clicked.connect(self._start_clicked)
        self.stop_btn.clicked.connect(self._stop_clicked)

        buttons_layout.addWidget(self.start_btn)
        buttons_layout.addWidget(self.stop_btn)

        root.addWidget(buttons_widget)

    def _start_clicked(self):
        self.is_recording = True
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        print('START RECORDING')

    def _stop_clicked(self):
        self.recordingStopRequested.emit()
        self.is_recording = False
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)

# === State Handlers (low-level, reusable) ===
async def acquire_fixation_func(context, get_gaze, center, accpt_gaze_radius_pix, monitorsubj_W_pix, monitorsubj_H_pix):
    # Wait until first center fixation - start of experiment
    reaquire_dur_s = 999999 # a very long duration to avoid passing ACQUIRE_FIXATION before acquiring the fixation cross
    while True:
        acquired = await wait_for(
            context,
            lambda: abs(QPoint.dotProduct(
                gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
                gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
            )) ** .5 < accpt_gaze_radius_pix,
            timedelta(seconds=reaquire_dur_s)
        )
        if acquired:
            return

async def initiate_func(context, get_gaze, center, accpt_gaze_radius_pix, duration, monitorsubj_W_pix, monitorsubj_H_pix):
  # Maintain fixation to initiate flashes for 'duration1' ms (if total duration not met, restart)
  success = await wait_for_hold(
      context,
      lambda: abs(QPoint.dotProduct(
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
      )) ** .5 < accpt_gaze_radius_pix,
      timedelta(seconds=duration), # initial fixation duration
      timedelta(seconds=0) # 0sec to ensure waiting indefinitely
  )
  return success

async def fixate_func(context, get_gaze, center, accpt_gaze_radius_pix, duration1, duration2, monitorsubj_W_pix, monitorsubj_H_pix):
  # Continuous fixation during flashes of checkerboards
  continuous_dur_s = 999999 # a very long duration to enable continuous fixation
  success = await wait_for_hold(
      context,
      lambda: abs(QPoint.dotProduct(
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
      )) ** .5 < accpt_gaze_radius_pix,
      timedelta(seconds=duration1), # continuous fixation duration - infinite
      timedelta(seconds=duration2) # allowed single blink duration
  )
  return success

# === State Phase Handlers (high-level, orchestration) ===
async def handle_acquire_fixation(
    context,
    get_gaze,
    center,
    accpt_gaze_radius_pix,
    monitorsubj_W_pix,
    monitorsubj_H_pix
    ):
    global state
    state = State.ACQUIRE_FIXATION
    await context.log('STATE=ACQUIRE_FIXATION')
    # print(state)
    # widget.update()
    await acquire_fixation_func(
        context,
        get_gaze,
        center,
        accpt_gaze_radius_pix,
        monitorsubj_W_pix,
        monitorsubj_H_pix
    )
    return

async def handle_initiate(
    context,
    get_gaze,
    center,
    accpt_gaze_radius_pix,
    init_duration,
    monitorsubj_W_pix,
    monitorsubj_H_pix,
    widget,
    converter
):
    global state
    state = State.INITIATE
    await context.log('STATE=INITIATE')
    # print(state)
    widget.update()
    success = await initiate_func(context, get_gaze, center, accpt_gaze_radius_pix, init_duration, monitorsubj_W_pix, monitorsubj_H_pix)
    return success

async def handle_fixate(
    context,
    get_gaze,
    center,
    accpt_gaze_radius_pix,
    fix_duration,
    blink_duration,
    monitorsubj_W_pix,
    monitorsubj_H_pix,
    widget,
    converter
):
    global state
    state = State.FIXATE
    await context.log('STATE=FIXATE')
    # print(state)
    widget.update()
    success = await fixate_func(context, get_gaze, center, accpt_gaze_radius_pix, fix_duration, blink_duration, monitorsubj_W_pix, monitorsubj_H_pix)
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
  """Main entry point for the task."""
  global converter, center, center_f, cross_x_pos, cross_y_pos, location_iter, checkerboard_locations_pix, \
        number_of_squares, square_size_pix, checkerboard_size_pix, \
        repetition, repeats_per_location, start, abort, current_location, abort_location, \
        trial_count, success_count, abort_count, reward_count, reward_total_released_ms, state, \
        fix_start, presentation_duration, rest_duration, \
        current_directory, abort_sound, success_sound, \
        gaze_success_store, gaze_failure_store, \
        step_size_deg, left_edge_deg, right_edge_deg, top_edge_deg, bottom_edge_deg, \
        photodiode_blinking_square, photodiode_static_square, WATCHING

  # Get the task configuration
  config = context.task_config
  monitorsubj_W_pix = config['monitorsubj_W_pix']
  monitorsubj_H_pix = config['monitorsubj_H_pix']
  monitorsubj_dist_m = config['monitorsubj_dist_m']
  monitorsubj_width_m = config['monitorsubj_width_m']

  cross_x_pos = config['cross_x_pos']
  cross_y_pos = 0 - config['cross_y_pos']
  
  def generate_checkerboard_locations(converter, config):
    """
    Generate a shuffled list of (x, y) pixel offsets for checkerboard locations.
    """
    step_size_deg = config['step_size_deg']
    left_edge_deg = config['left_edge_deg']
    right_edge_deg = config['right_edge_deg']
    top_edge_deg = config['top_edge_deg']
    bottom_edge_deg = config['bottom_edge_deg']

    step_size_pix = converter.deg_to_pixel_rel(step_size_deg)
    left_edge_pix = converter.deg_to_pixel_rel(left_edge_deg)
    right_edge_pix = converter.deg_to_pixel_rel(right_edge_deg)
    top_edge_pix = converter.deg_to_pixel_rel(top_edge_deg)
    bottom_edge_pix = converter.deg_to_pixel_rel(bottom_edge_deg)
    
    
    # Robust arange for any order of endpoints
    def arange_any_order(start, stop, step):
        if step == 0:
            raise ValueError("step_size cannot be zero")
        if start == stop:
            return np.array([start])
        elif (stop - start) * step > 0:
            # step is in the correct direction
            return np.arange(start, stop + (1 if step > 0 else -1), step)
        else:
            # step is in the wrong direction, flip it
            return np.arange(start, stop + (1 if step < 0 else -1), -step)

    # Generate grid of locations
    x_positions = arange_any_order(left_edge_pix, right_edge_pix, step_size_pix)# + int(converter.screen_pixels.width / 2) + int(cross_x_pos)
    y_positions = arange_any_order(bottom_edge_pix, top_edge_pix, step_size_pix) #+ int(converter.screen_pixels.height / 2) + int(cross_y_pos)
    # Invert y so that positive is up, negative is down accounting for QT's coordinate system
    checkerboard_location_pix = [(int(x), -int(y)) for x in x_positions for y in y_positions]
    random.shuffle(checkerboard_location_pix)
    return checkerboard_location_pix  

  if converter is None:    
    # If you turn on saving only after running >=1 trial, then 
    # converter = Converter(Size(1920, 1080), .5283, .57)
    converter = Converter(Size(monitorsubj_W_pix, monitorsubj_H_pix), monitorsubj_width_m, monitorsubj_dist_m)

    center = QPoint(int(converter.screen_pixels.width), int(converter.screen_pixels.height))/2 + QPoint(int(cross_x_pos), int(cross_y_pos))
    center_f = QPointF(float(converter.screen_pixels.width), float(converter.screen_pixels.height))/2 + QPointF(float(cross_x_pos), float(cross_y_pos))
    photodiode_blinking_square = QColor(255, 255, 255, 255) # Create a QColor object with white color and transparency control (i.e. alpha)
    photodiode_static_square = QColor(0, 0, 0, 255)

    # Adding sound files to the task
    current_directory = os.getcwd() # Get the current working directory
    # Define a relative path (e.g., accessing a file in a subdirectory)
    relative_path = os.path.join(current_directory, 'thalamus','task_controller', 'failure_clip.wav')
    abort_sound = QSound(relative_path) 
    # relative_path = os.path.join(current_directory, 'thalamus','task_controller', 'success_clip.wav')
    # success_sound = QSound(relative_path) # Load the .wav file (replace with your file path)

    number_of_squares = int(config['number_of_squares'])
    square_size_deg = (config['square_size'])
    square_size_pix = converter.deg_to_pixel_rel(square_size_deg)
    checkerboard_size_pix = number_of_squares*square_size_pix
    step_size_deg = config['step_size_deg']
    left_edge_deg = config['left_edge_deg']
    right_edge_deg = config['right_edge_deg']
    top_edge_deg = config['top_edge_deg']
    bottom_edge_deg = config['bottom_edge_deg']

    # Generate all checkerboard locations for this session
    checkerboard_locations_pix = generate_checkerboard_locations(converter, config)
    repeats_per_location = config['repeats_per_location']
    random.shuffle(checkerboard_locations_pix)  # Shuffle all locations for the session
    # print(checkerboard_locations_pix)
    location_iter = iter(checkerboard_locations_pix) # Create an iterator to cycle through locations
    repetition = 1
    start = 1

    def on_change(source, action, key, value):
      if not isinstance(source, ObservableDict):
        return
      
      if not source.get('type', None) == 'STORAGE':
        return
      
      if key == 'Running' and value: # do smthg when STORAGE gets switched on
        # context.trial_summary_data.used_values['photodiode_blinking_square_color_rgba'] = [photodiode_blinking_square.red(), \
        #     photodiode_blinking_square.green(), photodiode_blinking_square.blue(), photodiode_blinking_square.alpha()]
        # context.trial_summary_data.used_values['photodiode_static_square_color_rgba'] = [photodiode_static_square.red(), \
        #     photodiode_static_square.green(), photodiode_static_square.blue(), photodiode_static_square.alpha()]   
        context.trial_summary_data.used_values['code_name_and_version'] = ["RF_V1"]

    if not WATCHING:
      context.config.add_recursive_observer(on_change) # this method is responsible for registering 
      # "on_change()" as a listener for changes. "on_change" will be executed automatically by the
      # add_recursive_observer() mechanism whenever the relevant event occurs
      config = context.task_config
      WATCHING = True

    await context.log(f"CHECKBOX LOCATIONS={checkerboard_locations_pix}")

  reward_every_s = context.get_value('reward_frequency_s')
  reward_frequency = 1/reward_every_s
  reward_ms = context.get_value('reward_ms')

  accpt_gaze_radius_deg = config['accpt_gaze_radius_deg'] 

  center = QPoint(int(converter.screen_pixels.width), int(converter.screen_pixels.height))/2 + QPoint(int(cross_x_pos), int(cross_y_pos))
  center_f = QPointF(float(converter.screen_pixels.width), float(converter.screen_pixels.height))/2 + QPointF(float(cross_x_pos), float(cross_y_pos))

  # Get the checkerboard configuration from the task config
  init_duration = config['init_duration']/1000 
  presentation_duration = config['presentation_duration']/1000 
  rest_duration = config['rest_duration']/1000 
  penalty_delay = config['penalty_delay'] / 1000 
  blink_duration = config['blink_duration']/100 
  
  number_of_squares = int(config['number_of_squares'])
  square_size_deg = (config['square_size'])
  square_size_pix = converter.deg_to_pixel_rel(square_size_deg)
  checkerboard_size_pix = number_of_squares*square_size_pix

  # Calculate half the length of the cross arms in pixels for 2 degree  
  half_cross_len_pix = converter.deg_to_pixel_rel(0.25)  # 0.25 deg each side, total 0.5 deg 
  # Create the cross centered at (center.x(), center.y())
  cross = QPainterPath()
  cross.moveTo(center.x() - half_cross_len_pix, center.y())
  cross.lineTo(center.x() + half_cross_len_pix, center.y())
  cross.moveTo(center.x(), center.y() - half_cross_len_pix)
  cross.lineTo(center.x(), center.y() + half_cross_len_pix)
  
  # Get variables from the config
  init_duration = config['init_duration']/1000
  presentation_duration = config['presentation_duration']/1000
  rest_duration = config['rest_duration']/1000
  penalty_delay = config['penalty_delay'] / 1000
  # penalty_delay = context.get_value('penalty_delay') / 1000
  blink_duration = config['blink_duration']/100

  reward_every_s = context.get_value('reward_frequency_s')
  reward_frequency = 1/reward_every_s
  reward_ms = context.get_value('reward_ms')

  accpt_gaze_radius_deg = config['accpt_gaze_radius_deg']
  accpt_gaze_radius_pix = converter.deg_to_pixel_rel(accpt_gaze_radius_deg)

  background_color = config['background_color']
  background_color_qt = QColor(background_color[0], background_color[1], background_color[2], 255)
  

  if number_of_squares != int(config['number_of_squares']) or \
    square_size_deg != (config['square_size']) or \
    square_size_pix != converter.deg_to_pixel_rel(square_size_deg) or \
    init_duration != config['init_duration']/1000 or \
    presentation_duration != config['presentation_duration']/1000 or \
    rest_duration != config['rest_duration']/1000 or \
    penalty_delay != config['penalty_delay'] / 1000 or \
    blink_duration != config['blink_duration']/100 or \
    step_size_deg != config['step_size_deg'] or \
    left_edge_deg != config['left_edge_deg'] or \
    right_edge_deg != config['right_edge_deg'] or \
    top_edge_deg != config['top_edge_deg'] or \
    bottom_edge_deg != config['bottom_edge_deg']:
     
    number_of_squares = int(config['number_of_squares']) 
    square_size_deg = (config['square_size']) 
    square_size_pix = converter.deg_to_pixel_rel(square_size_deg) 
    init_duration = config['init_duration']/1000 
    presentation_duration = config['presentation_duration']/1000 
    rest_duration = config['rest_duration']/1000 
    penalty_delay = config['penalty_delay'] / 1000 
    blink_duration = config['blink_duration']/100 
    step_size_deg = config['step_size_deg'] 
    left_edge_deg = config['left_edge_deg'] 
    right_edge_deg = config['right_edge_deg'] 
    top_edge_deg = config['top_edge_deg'] 
    bottom_edge_deg = config['bottom_edge_deg']
  
  # feedback circle
  # radius_of_green_feedback_circle_pix = converter.deg_to_pixel_rel(config['accpt_gaze_radius_deg'])

  def draw_checkerboard(
      painter: QPainter,
      center_x: int,
      center_y: int,
      number_of_squares: int,
      square_size_pix: int
  ):
      # Calculate top-left corner
      # checkerboard_size_pix = number_of_squares * square_size_pix
      x0 = center_x - checkerboard_size_pix // 2
      y0 = center_y - checkerboard_size_pix // 2
      for i in range(number_of_squares):
          for j in range(number_of_squares):
              color = QColor(255, 255, 255) if (i + j) % 2 == 0 else QColor(0, 0, 0)
              painter.fillRect(
                  QRect(int(x0 + j * square_size_pix),
                  int(y0 + i * square_size_pix),
                  int(square_size_pix),
                  int(square_size_pix)),
                  color
              )

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
    painter.drawText(rect, Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop, text)  # Draw the text within the rectangle
    painter.restore()  # Restore the painter to its previous state

  def draw_gaze(painter, gaze_qpoint, color_rgba):
    path = QPainterPath()
    gaze_f = QPointF(gaze_qpoint)
    path.addEllipse(gaze_f, 12, 12)
    painter.fillPath(path, color_rgba) 

  widget: CanvasProtocol = context.widget
  assert widget is not None

  # Initialize gaze position
  gaze = QPoint(99999,99999)
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

  def get_next_location():
      global location_iter, repetition, checkerboard_locations_pix, repeats_per_location
      try:
          return next(location_iter)
      except StopIteration:
          # All locations exhausted, reshuffle and restart
          repetition += 1
          if repetition > repeats_per_location:
            context.log("END OF SESSION")
            return TaskResult(True)
          random.shuffle(checkerboard_locations_pix)
          location_iter = iter(checkerboard_locations_pix)
          return next(location_iter)
  
  if abort == 1:
     current_location = abort_location
  else:
     current_location = get_next_location()
     abort_location = current_location
  await context.log(f"CURRENT CHECKERBOARD LOCATION]={current_location}")

  fix_start = None
  def renderer(painter: QPainter):
    global state, fix_start, cross_x_pos, cross_y_pos, center_f, success_count, presentation_duration, rest_duration
    center_x = int(converter.screen_pixels.width / 2) + int(cross_x_pos)
    center_y = int(converter.screen_pixels.height / 2) + int(cross_y_pos)
    painter.fillRect(QRect(0, 0, 4000, 4000), background_color_qt) # QColor(128, 128, 128, 255); make the background of desired color
    painter.fillRect(QRect(int(widget.width()) - 150, int(widget.height()) - 150, 150, 150), photodiode_static_square) # background small square bottom-right
    # Clearing Operator View canvas
    if getattr(context.widget, 'do_clear', False):
      print('CLEAR OPERATOR VIEW')
      context.widget.do_clear = False

    # Draw fixation cross - always
    pen = painter.pen()
    pen.setWidth(2)
    pen.setColor(QColor(30, 30, 255))
    painter.setPen(pen)
    painter.drawPath(cross)
    
    if state == State.FIXATE:
      if fix_start is None:
        fix_start = datetime.now()
      if (datetime.now()-fix_start).total_seconds() <= presentation_duration:
        checkerboard_x_pix = int(center_x + current_location[0])
        checkerboard_y_pix = int(center_y + current_location[1])
        draw_checkerboard(
            painter,
            checkerboard_x_pix,
            checkerboard_y_pix,
            number_of_squares,
            square_size_pix
          )
        painter.fillRect(int(widget.width() - 100), int(widget.height()-100), 100, 100, photodiode_blinking_square) # photodiode white square presentation
        rest_start = datetime.now()
      elif (datetime.now()-fix_start).total_seconds() <= presentation_duration + rest_duration and (datetime.now()-fix_start).total_seconds() > presentation_duration:
          pen = painter.pen()
          pen.setWidth(2)
          pen.setColor(QColor(30, 30, 255))
          painter.setPen(pen)
          painter.drawPath(cross)
      else:
          fix_start = None

    # Draw only in the OPERATOR view
    with painter.masked(RenderOutput.OPERATOR): # using a context manager to temporarily change the drawing behavior of the painter object
      # A feature of the operator view is that you can draw stuff only for the operator into it. Anything in this 
      # with painter.masked(RenderOutput.OPERATOR) block will only appear in the operator view.
      
      painter.fillRect(QRect(0, 0, 450, 160), QColor(255, 255, 255, 255)) # background white rectangle in the OView to see text

      path = QPainterPath()
      path.addEllipse(center_f, accpt_gaze_radius_pix, accpt_gaze_radius_pix)
      painter.fillPath(path, QColor(255, 255, 255, 128))

      # Drawing the gaze position as a continuously moving point
      color_rgba = QColor(138, 43, 226, 255) # purple
      draw_gaze(painter, gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), color_rgba)

      # Drawing text message on the operator view
      drawText(painter, f"ITERATION = {trial_count}/{int(len(checkerboard_locations_pix)*repeats_per_location)}", QPoint(0, 10), background_color_qt) 
      drawText(painter, f"SUCCESS_COUNT = {success_count}", QPoint(0, 40), background_color_qt) 
      drawText(painter, f"ABORT_COUNT = {abort_count}", QPoint(0, 70), background_color_qt)
      drawText(painter, f"TOTAL REWARD = {round(reward_total_released_ms)} ms", QPoint(0, 100), background_color_qt) 
      temp_gaze = gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix)
      drawn_text = f"({temp_gaze.x()}, {temp_gaze.y()})"
      drawText(painter, drawn_text, temp_gaze, background_color_qt) 
      drawText(painter, f"Gaze (pix): x = {temp_gaze.x()}, y = {temp_gaze.y()}", QPoint(0, 130), background_color_qt)

      # Drawing all previously painted gazes of failed target holding
      # for gaze_qpoint, color_rgba in gaze_failure_store:
      #   draw_gaze(painter, gaze_qpoint, color_rgba)
      # # Drawing all previously painted gazes of successful target holding
      # for gaze_qpoint, color_rgba in gaze_success_store:
      #   draw_gaze(painter, gaze_qpoint, color_rgba)

  # Set the renderer function to the widget's renderer
  widget.renderer = renderer
  # Store the context information into .tha file
  # await context.log(json.dumps(context.config)) 

  # Handles State.ACQUIRE_FIXATION
  await handle_acquire_fixation(context, lambda: gaze, center, accpt_gaze_radius_pix, monitorsubj_W_pix, monitorsubj_H_pix)

  # Handles State.INITIATE
  if abort == 1 or start == 1:
    success = await handle_initiate(context, lambda: gaze, center, accpt_gaze_radius_pix, init_duration, monitorsubj_W_pix, monitorsubj_H_pix, widget, converter)
    start = 0

  # Handles State.FIXATE

  fix_duration = presentation_duration + rest_duration
  success = await handle_fixate(context, lambda: gaze, center, accpt_gaze_radius_pix, fix_duration, blink_duration, monitorsubj_W_pix, monitorsubj_H_pix, widget, converter)
  success_count += 1

  if not success:
    gaze_failure_store.append((gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), QColor(255, 69, 0, 128)))
    await context.log('TrialResult=ABORT') # saving any variables / data from code
    abort_count += 1
    success_count -= 1
    state = State.ABORT
    # print(state)
    abort_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    abort = 1
    reward_count = 0
    return TaskResult(False)

  gaze_success_store.append((gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), QColor(0, 255, 0, 128)))
  await context.log('TrialResult=SUCCESS') # saving any variables / data from code
  state = State.SUCCESS
  abort = 0

  # print(state)
  # success_sound.play()
  # await context.sleep(timedelta(seconds=1)) # 1s delay to allow playing the sound; sound doesn't play without this delay
  trial_count += 1
  reward_count += 1
  reward_interval_trls = round(1/reward_frequency)
  if reward_count >= reward_interval_trls:
    reward_total_released_ms += reward_ms
    await context.log("RELEASE REWARD = %d ms, TOTAL RELEASED = %d ms"%(int(reward_ms), \
                                                                        int(reward_total_released_ms)) )
    create_task_with_exc_handling(context.inject_analog('reward_in', AnalogResponse(
      data=[5,0], # 5 = HIGH, 0 = LOW voltages
      spans=[Span(begin=0,end=2,name='Reward')], 
      sample_intervals=[1_000_000*int(reward_ms)]) # multiplying by 1_000_000 will give us nanoseconds (ns)
    ))
    reward_count = 0

  # "TaskResult" is used to determine whether the trial is or is not removed from the queue
  # If TaskResult(False), the trial is not removed from the queue. If TaskResult(True), the trial is removed from the queue.
  return TaskResult(False)


