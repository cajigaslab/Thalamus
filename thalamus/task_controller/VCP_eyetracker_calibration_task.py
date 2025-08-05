"""
Implementation of the VCP Eyetracker Manual Calibration task v1.0 (2025/07/31)
"""
import time
import typing
import numbers
import logging
from datetime import timedelta
import random
import numpy as np # import Numpy to draw Gaussian
import asyncio

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
circle_radii = []
rand_pos_i = 0
trial_num = 0
trial_saccade_count = 0
trial_saccade_success_count = 0
reward_total_released_ms = 0
trial_catch_count = 0
trial_catch_success_count = 0
drawn_objects = []
rand_pos = []
gaze_success_store = []
gaze_failure_store = []
current_target_index = 0

WATCHING = False

# === Utility Functions ===
# <<<<<<<<<<<<<<<<<<<<<<<<<
def gaussian_gradient(center: QPointF, background_color_qt: QColor, radius: float, deviations: float = 1, \
                      brightness_in: int = 255, luminance_percent: float = 1.0):
  """Create a radial Gaussian gradient."""
  gradient = QRadialGradient(center, radius)
  resolution = 1000
  for i in range(resolution):
    # brightness is calculated using input luminance as a percentage of the background to maintain Gaussians between the values of 255...gradient_background
    brightness = int((brightness_in - background_color_qt.red()) * luminance_percent/100 + background_color_qt.red()) 
    if background_color_qt.red() == 0 and background_color_qt.green() == 0 and background_color_qt.blue() == 0: # if black background
      level = int(brightness * np.exp(-((deviations * i / resolution) ** 2) / 2)) # formula without adjustment for background color
    else:
      level = int(background_color_qt.red() + (brightness - background_color_qt.red())*np.exp(-(deviations*i/resolution)**2/(2))) # version that makes Gaussian colors bound by background and draws 2 colors: Black and White
    gradient.setColorAt(i/resolution, QColor(level, level, level))
  gradient.setColorAt(1, QColor(background_color_qt.red(), background_color_qt.green(), background_color_qt.blue(), 0)) #Qt.GlobalColor.black
  return gradient

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
# >>>>>>>>>>>>>>>>>>>>>>>

# === State Definitions ===
class State(enum.Enum):
  """Enumeration for task states."""
  CENTER = enum.auto()
  TARGET1 = enum.auto()
  TARGET2 = enum.auto()
  TARGET3 = enum.auto()
  TARGET4 = enum.auto()

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
    Form.Constant('How many times repeat peripheral targets', 'target_repeat_n', 1),
    # Form.Uniform('\U0001F4A7 Reward per trial', 'reward_pertrial_ms', 10, 350, 'ms'),
    Form.Uniform('\u2194 Target width', 'width_targ_deg', 2, 2, '\u00B0'),
    Form.Constant('\u2194 Target width step', 'widthtargdeg_step', 0.1, '\u00B0'),
    Form.Uniform('\u2195 Target height', 'height_targ_deg', 2, 2, '\u00B0'),
    Form.Constant('\u2195 Target height step', 'heighttargdeg_step', 0.1, '\u00B0'),
    Form.Bool('\u2194\u2195 Lock Height to Width?', 'is_height_locked', True),
    Form.Uniform('\U0001F9ED Target orientation (0-150)', 'orientation_targ_ran', 0, 150, '\u00B0'),
    Form.Constant('\U0001F9ED Target orientation step size (0-150)', 'orientation_targ_step', 30, '\u00B0'),
    Form.Uniform('\U0001F526 Target luminence (0-100)', 'luminance_targ_per', 100, 100,'%'),
    Form.Constant('\U0001F526 Target luminence step size (0-100)', 'luminance_targ_step', 10,'%'),
    # For LG24GQ50B-B with height of 1080 pix and at 0.57 m distance conversion factor of 0.0259 deg/pix, 
    # the largest diameter of the screen area to display targets is int(1080 pix * 0.0259 deg/pix) = 27 deg
    Form.Constant('\u25EF Radius for gaze acceptance', 'accpt_gaze_radius_deg', 2, '\u00B0'), # Define the radius in degrees of the area where gaze is accepted as being correct
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
    Form.Color('Background Color', 'background_color', QColor(31, 31, 31, 255)),
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
  # reward_pertrial_ms_min_spinbox = form.findChild(QDoubleSpinBox, "reward_pertrial_ms_min")
  # reward_pertrial_ms_min_spinbox.setRange(10, 500)
  # reward_pertrial_ms_min_spinbox.setSingleStep(1)
  # reward_pertrial_ms_max_spinbox = form.findChild(QDoubleSpinBox, "reward_pertrial_ms_max")
  # reward_pertrial_ms_max_spinbox.setRange(100, 500)
  # reward_pertrial_ms_max_spinbox.setSingleStep(1)
  widthtargdeg_step_spinbox = form.findChild(QDoubleSpinBox, "widthtargdeg_step")
  widthtargdeg_step_spinbox.setRange(.1, 1.0)
  widthtargdeg_step_spinbox.setSingleStep(.1)
  heighttargdeg_step_spinbox = form.findChild(QDoubleSpinBox, "heighttargdeg_step")
  heighttargdeg_step_spinbox.setRange(.1, 1.0)
  heighttargdeg_step_spinbox.setSingleStep(.1)
  orientation_targ_step_spinbox = form.findChild(QDoubleSpinBox, "orientation_targ_step")
  orientation_targ_step_spinbox.setRange(1, 150)
  orientation_targ_step_spinbox.setSingleStep(1)
  orientation_targ_ran_min_spinbox = form.findChild(QDoubleSpinBox, "orientation_targ_ran_min")
  orientation_targ_ran_min_spinbox.setRange(0, 150)
  orientation_targ_ran_min_spinbox.setSingleStep(15)  
  orientation_targ_ran_max_spinbox = form.findChild(QDoubleSpinBox, "orientation_targ_ran_max")
  orientation_targ_ran_max_spinbox.setRange(0, 150)
  orientation_targ_ran_max_spinbox.setSingleStep(15)
  accpt_gaze_radius_deg_spinbox = form.findChild(QDoubleSpinBox, "accpt_gaze_radius_deg")
  accpt_gaze_radius_deg_spinbox.setRange(.1, 60.0)
  accpt_gaze_radius_deg_spinbox.setSingleStep(0.1)
  luminance_targ_step_spinbox = form.findChild(QDoubleSpinBox, "luminance_targ_step")
  luminance_targ_step_spinbox.setRange(1, 100)
  luminance_targ_step_spinbox.setSingleStep(1)
  luminance_targ_per_min_spinbox = form.findChild(QDoubleSpinBox, "luminance_targ_per_min")
  luminance_targ_per_min_spinbox.setRange(0, 100)
  luminance_targ_per_min_spinbox.setSingleStep(5)  
  luminance_targ_per_max_spinbox = form.findChild(QDoubleSpinBox, "luminance_targ_per_max")
  luminance_targ_per_max_spinbox.setRange(0, 100)
  luminance_targ_per_max_spinbox.setSingleStep(5)  
  width_targ_deg_min_spinbox = form.findChild(QDoubleSpinBox, "width_targ_deg_min")
  width_targ_deg_min_spinbox.setRange(0.1, 10)
  width_targ_deg_min_spinbox.setSingleStep(0.1)  
  width_targ_deg_max_spinbox = form.findChild(QDoubleSpinBox, "width_targ_deg_max")
  width_targ_deg_max_spinbox.setRange(0.1, 10)
  width_targ_deg_max_spinbox.setSingleStep(0.1)  
  height_targ_deg_min_spinbox = form.findChild(QDoubleSpinBox, "height_targ_deg_min")
  height_targ_deg_min_spinbox.setRange(0.1, 10)
  height_targ_deg_min_spinbox.setSingleStep(0.1)  
  height_targ_deg_max_spinbox = form.findChild(QDoubleSpinBox, "height_targ_deg_max")
  height_targ_deg_max_spinbox.setRange(0.1, 10)
  height_targ_deg_max_spinbox.setSingleStep(0.1)  

  # Code below is used to update the label and freeze the GUI field when we lock the height to the width
  w_label = form.findChild(QLabel, "monitorsubj_W_pix_label")
  w_label_original = w_label.text()
  i = 0

  def on_change(source, action, key, value):
    nonlocal i
    if key == 'is_height_locked': # If the height is locked to the width, then disable the height spinboxes
      height_targ_deg_min_spinbox.setEnabled(not value)
      height_targ_deg_max_spinbox.setEnabled(not value)
    elif key in ('monitorsubj_W_pix', 'monitorsubj_H_pix'): # If the monitor size is changed, then update the value in degrees
      # add the rest of the 3 monitor dimensions
      converter0 = Converter(Size(task_config['monitorsubj_W_pix'], task_config['monitorsubj_H_pix']), \
                             task_config['monitorsubj_width_m'], task_config['monitorsubj_dist_m']) # replace variables with GUI inputs!!
      # i += 1 # Replace i with conversion function into degrees
      i = converter0.relpix_to_absdeg(task_config['monitorsubj_H_pix'], task_config['monitorsubj_W_pix'])
      w_label.setText(w_label_original + f' ({round(i[1], 2)} deg)') # add (... deg) to the label
      converter0 = None # destroy the converter just in case
 
  task_config.add_recursive_observer(on_change, lambda: isdeleted(result), True)

  return result


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
  global converter, center, center_f, circle_radii, rand_pos_i, \
    trial_num, trial_saccade_count, trial_saccade_success_count, trial_catch_count, \
    trial_catch_success_count, drawn_objects, rand_pos, reward_total_released_ms, \
    gaze_success_store, gaze_failure_store, failure_sound, abort_sound, success_sound, \
    photodiode_blinking_square, photodiode_static_square, WATCHING, state, current_target_index, \
    target_loc_eccentricity_pix_min, target_loc_eccentricity_pix_max

  target_states = [State.TARGET1, State.TARGET2, State.TARGET3, State.TARGET4]
  presses = 0
  state = State.CENTER

  # Get the task configuration
  config = context.task_config
  monitorsubj_W_pix = config['monitorsubj_W_pix']
  monitorsubj_H_pix = config['monitorsubj_H_pix']
  monitorsubj_dist_m = config['monitorsubj_dist_m']
  monitorsubj_width_m = config['monitorsubj_width_m']
  target_repeat_n = config['target_repeat_n']

  # Manually switching between targets using the button in the Operator View
  if getattr(context.widget, 'next_target', False):
    # Advance to next target state
    current_target_index = (current_target_index + 1) % len(target_states)
    state = target_states[current_target_index]
    print(f"Switched to: {target_states[current_target_index]}")
    context.widget.next_target = False  # Reset the flag
    # success = True # Initialize success variable
    # return TaskResult(True)

  """Check if target location angle lies within the user-defined sector [min, max] (counterclockwise)."""
  def angle_in_sector(angle, sector_min, sector_max):
    """Return True if angle is within [sector_min, sector_max) (inclusive lower, exclusive upper), handling wrap-around."""
    angle = angle % 360
    sector_min = sector_min % 360
    sector_max = sector_max % 360
    if sector_min < sector_max:
        return sector_min <= angle < sector_max
    elif sector_min > sector_max:
        return angle >= sector_min or angle < sector_max
    else:
        # to support "full circle" sector, but could also be zero-width sector
        return True

  def get_valid_angles(step_deg, sector1_min, sector1_max, sector2_min, sector2_max):
      all_angles = [i for i in range(0, 360, int(step_deg))]
      valid_angles = []
      for angle in all_angles:
          if (angle_in_sector(angle, sector1_min, sector1_max) or
              angle_in_sector(angle, sector2_min, sector2_max)):
              valid_angles.append(angle)

      return sorted(set(valid_angles))  # remove duplicates if overlapping

  if converter is None: 
    # Everything below this line will be executed only once, when the task is started   
    # If you turn on saving only after running >=1 trial, then 
    # converter = Converter(Size(1920, 1080), .5283, .57)
    converter = Converter(Size(monitorsubj_W_pix, monitorsubj_H_pix), monitorsubj_width_m, monitorsubj_dist_m)
    center = QPoint(int(converter.screen_pixels.width), int(converter.screen_pixels.height))/2
    center_f = QPointF(float(converter.screen_pixels.width), float(converter.screen_pixels.height))/2

    state = target_states[0] # Initialize the state to CENTER

    trial_num = 0
    valid_angles_deg = {45, 135, 225, 315} # Get valid angles in degrees
    valid_angles_rad = [np.deg2rad(a) for a in valid_angles_deg] # Convert degrees to radians

    # Generate random positions around a circle
    radius = converter.deg_to_pixel_rel(6)
    rand_pos = [
      (center.x() + radius*np.cos(angle), center.y() - radius*np.sin(angle))
      for angle in valid_angles_rad # Use pre-generated valid angles
    ]

    # Create a QColor object for the photodiode blinking square and static square
    photodiode_blinking_square = QColor(255, 255, 255, 255) # Create a QColor object with white color and transparency control (i.e. alpha)
    photodiode_static_square = QColor(0, 0, 0, 255)
   
    def on_change(source, action, key, value):
      if not isinstance(source, ObservableDict):
        return

    if not WATCHING:
      context.config.add_recursive_observer(on_change) # this method is responsible for registering 
      # "on_change()" as a listener for changes. "on_change" will be executed automatically by the
      # add_recursive_observer() mechanism whenever the relevant event occurs
      WATCHING = True
 

  """ Below are commands that will be executed on every trial """


  # If all random positions have been used, shuffle the list and reset the index
  if rand_pos_i == len(rand_pos):
    rand_pos_i = 0

  # Define the vertices for the fixation cross in degrees
  vertices_deg = [ 
      (-0.5, 0), (0.5, 0),  # Horizontal line
      (0, -0.5), (0, 0.5)  # Vertical line
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
  accpt_gaze_radius_deg = config['accpt_gaze_radius_deg']
  accpt_gaze_radius_pix = converter.deg_to_pixel_rel(accpt_gaze_radius_deg)
  is_height_locked = config['is_height_locked']
  target_color_rgb = config['target_color']
  background_color = config['background_color']
  background_color_qt = QColor(background_color[0], background_color[1], background_color[2], 255)

  def pick_random_value(min_val, max_val, step):
    # Generate the range of possible values using numpy
    possible_values = np.arange(min_val, max_val + step, step).tolist()
    # Pick a random value from the possible values
    return random.choice(possible_values)

  # Create a Gaussian gradient for the target with randomly selected luminance, orientation, size and location
  # Random selection is based on user-defined range min...max and step size
  # reward_pertrial_ms = context.get_value('reward_pertrial_ms') # return a uniform random number
  luminance_targ_per = pick_random_value(config['luminance_targ_per']['min'], config['luminance_targ_per']['max'], config['luminance_targ_step'])
  # context.trial_summary_data.used_values['luminance_targ_per'] = luminance_targ_per # this command based on 'get_value()' from 'task_context.py' aalows to add values to task_config['used_values']
  orientation_targ_ran = pick_random_value(config['orientation_targ_ran']['min'], config['orientation_targ_ran']['max'], config['orientation_targ_step'])
  # context.trial_summary_data.used_values['orientation_targ_ran'] = orientation_targ_ran 
  width_targ_pix = converter.deg_to_pixel_rel(pick_random_value(config['width_targ_deg']['min'], config['width_targ_deg']['max'], config['widthtargdeg_step']))
  # context.trial_summary_data.used_values['width_targ_pix'] = width_targ_pix 
  if is_height_locked:
    height_targ_pix = width_targ_pix
  else:
    height_targ_pix = converter.deg_to_pixel_rel(pick_random_value(config['height_targ_deg']['min'], config['height_targ_deg']['max'], config['heighttargdeg_step']))
  # context.trial_summary_data.used_values['height_targ_pix'] = height_targ_pix 

  gaussian = gaussian_gradient(QPointF(0, 0), background_color_qt, width_targ_pix/2, 3, 255, luminance_targ_per)

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

  def renderer(painter: QPainter):
    global state
    nonlocal gaussian
    painter.fillRect(QRect(0, 0, 4000, 4000), background_color_qt) # QColor(128, 128, 128, 255); make the background of desired color
    painter.fillRect(int(widget.width() - 150), int(widget.height() - 150), 150, 150, photodiode_static_square) # background small square bottom-right

    # Draw only the fixation cross for State.CENTER
    if state == State.CENTER:
        pen = painter.pen()
        pen.setWidth(2)
        pen.setColor(QColor(255, 0, 0))
        painter.setPen(pen)
        painter.drawPath(cross)
        # Do NOT try to access state_to_index or draw the target!
        # You may want to return here to avoid further code execution
        targetpos_pix = center
        targetpos_pix_f = center_f

    # Draw target for each quadrant state
    elif state in (State.TARGET1, State.TARGET2, State.TARGET3, State.TARGET4):
        pen = painter.pen()
        pen.setWidth(2)
        pen.setColor(QColor(255, 0, 0))
        painter.setPen(pen)

        # Map state to rand_pos index
        state_to_index = {
            State.TARGET1: 0,  # top right
            State.TARGET2: 1,  # top left
            State.TARGET3: 2,  # bottom left
            State.TARGET4: 3,  # bottom right
        }
        idx = state_to_index[state]
        targetpos_pix = QPoint(int(rand_pos[idx][0]), int(rand_pos[idx][1]))
        targetpos_pix_f = QPointF(float(rand_pos[idx][0]), float(rand_pos[idx][1]))

        def draw_gaussian(painter: QPainter):
            # region -- Drawing version that plots only 1 current Gaussian
            painter.save()
            painter.translate(targetpos_pix)
            painter.rotate(orientation_targ_ran) #Apply rotation to gaussian
            painter.scale(1, height_targ_pix/width_targ_pix) # Apply X,Y scaling to gaussian; initial diameter is equal to width/2, hence need to scale only Y
            painter.fillRect(int(-widget.width()/2), int(-widget.height()/2), int(widget.width()), int(widget.height()), gaussian)
            # painter.fillRect draws the gaussian into a giant rectangle centered at the upper left corner of the screen 
            # and the above lines will stretch, rotate (if uncommented), and translate the gaussian to the correct place.
            painter.restore()
            # endregion

        # Draw the target (e.g., as a filled ellipse or your gaussian)
        pen = painter.pen()
        pen.setWidth(2)
        pen.setColor(QColor(255, 0, 0))
        painter.setPen(pen)
        draw_gaussian(painter)

    # Clearing Operator View canvas using button in the Operator View
    if getattr(context.widget, 'do_clear', False):
      # print('DO CLEAR')
      gaze_failure_store.clear()
      gaze_success_store.clear()      
      context.widget.do_clear = False

    # Draw the shadings around targets in the OPERATOR view indicating the areas where responses are accepted as correct
    with painter.masked(RenderOutput.OPERATOR): # using a context manager to temporarily change the drawing behavior of the painter object
      # A feature of the operator view is that you can draw stuff only for the operator into it. Anything in this 
      # with painter.masked(RenderOutput.OPERATOR) block will only appear in the operator view.
      
      painter.fillRect(QRect(0, 0, 465, 60), QColor(255, 255, 255, 255)) # background white rectangle in the OView to see text

      path = QPainterPath()
      path.addEllipse(targetpos_pix_f, accpt_gaze_radius_pix, accpt_gaze_radius_pix)
      painter.fillPath(path, QColor(255, 255, 255, 128))
      # path = QPainterPath() # Shading around the center indicating acceptance radius
      # path.addEllipse(center_f, accpt_gaze_radius_pix, accpt_gaze_radius_pix)
      # painter.fillPath(path, QColor(255, 255, 255, 128))

      # Drawing the gaze position as a continuously moving point
      color_rgba = QColor(138, 43, 226, 255) # purple
      draw_gaze(painter, gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), color_rgba)

      # Drawing text message on the operator view
      temp_gaze = gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix)
      drawn_text = f"({temp_gaze.x()}, {temp_gaze.y()})"
      drawText(painter, drawn_text, temp_gaze, background_color_qt) # Draw the text message
      drawText(painter, f"Gaze (pix): x = {temp_gaze.x()}, y = {temp_gaze.y()}", QPoint(0, 10), background_color_qt)

  # Set the renderer function to the widget's renderer
  widget.renderer = renderer

  if presses == 0:
    # If this is the first time running the task, begin by presenting just a fixation cross
    state = State.CENTER
    await asyncio.sleep(0.01)  # Yield control, check every 10ms
    widget.next_target = False

  while presses < target_repeat_n * len(target_states) + 1 :
    # Wait for the Next button to be pressed
    while not getattr(widget, 'next_target', False):
        await asyncio.sleep(0.01)  # Yield control, check every 10ms

    # Advance to next state
    current_target_index = (current_target_index + 1) % len(target_states)
    state = target_states[current_target_index]
    widget.next_target = False  # Reset the flag
    presses += 1
  
  # Wait for the Next button to be pressed
  while not getattr(widget, 'next_target', False):
      await asyncio.sleep(0.01)  # Yield control, check every 10ms
      state = State.CENTER
  widget.next_target = False  # Reset the flag

  # trial counters before the 1st "return" statement
  trial_num += 1 # Increment the trial counter
  print(f"Started trial # {trial_num}")
  await context.log(f"StartedTRIAL_NUM={trial_num}") # saving any variables / data from code
  converter = None # Reset the converter to allow re-initialization in the next trial
  return TaskResult(True)
  # "TaskResult" is used to determine whether the trial is or is not removed from the queue
  # If TaskResult(False), the trial is not removed from the queue. If TaskResult(True), the trial is removed from the queue.


