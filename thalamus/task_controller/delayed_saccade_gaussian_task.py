"""
Implementation of the delayed saccade Gaussian task v2.0 (2025/04/15)
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
task_groups = ['Delayed Saccade - Photic', 'Delayed Saccade - Catch', 'Saccade - No Delay'] # Define the possible task types

# Define the framerate and frame interval for the task
FRAMERATE = 60
INTERVAL = 1/FRAMERATE

converter = None
center = None
center_f = None
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
  FIXATE1 = enum.auto()
  TARGET_PRESENTATION = enum.auto()
  FIXATE2 = enum.auto()
  ACQUIRE_TARGET = enum.auto()
  HOLD_TARGET = enum.auto()
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
    Form.Choice('Task group', 'task_group', list(zip(task_groups, task_groups))),                
    Form.Bool('\u2728 Display target during saccade?', 'target_doesnt_disappear', False),
    Form.Uniform('\u23F0 Fixation Duration 1', 'fix1_duration', 1000, 2000, 'ms'),
    Form.Uniform('\u23F0 Target Presentation Duration', 'target_present_dur', 2000, 4000, 'ms'), 
    Form.Uniform('\u23F0 Fixation Duration 2', 'fix2_duration', 1000, 2000, 'ms'),
    Form.Uniform('\u23F0 Target Hold Duration', 'targethold_duration', 1000, 2000, 'ms'),
    Form.Uniform('\u23F0 Decision Temeout', 'decision_timeout', 1000, 2000, 'ms'),
    Form.Uniform('\u23F0 Penalty Delay', 'penalty_delay', 3000, 3000, 'ms'),
    Form.Constant('\u23F0 Max allowed single blink duration', 'blink_dur_ms', 500, 'ms'),                    
    Form.Uniform('\U0001F4A7 Reward per trial', 'reward_pertrial_ms', 10, 350, 'ms'),
    Form.Uniform('\u2194 Target width (0.1-1.0)', 'width_targ_deg', 0.1, 1, '\u00B0'),
    Form.Constant('\u2194 Target width step (0.1-1.0)', 'widthtargdeg_step', 0.1, '\u00B0'),
    Form.Uniform('\u2195 Target height (0.1-1.0)', 'height_targ_deg', 0.1, 1, '\u00B0'),
    Form.Constant('\u2195 Target height step (0.1-1.0)', 'heighttargdeg_step', 0.1, '\u00B0'),
    Form.Bool('\u2194\u2195 Lock Height to Width?', 'is_height_locked', False),
    Form.Bool('Paint location grid and accumulated targets?', 'paint_all_targets', False),
    Form.Uniform('\U0001F9ED Target orientation (0-150)', 'orientation_targ_ran', 0, 150, '\u00B0'),
    Form.Constant('\U0001F9ED Target orientation step size (0-150)', 'orientation_targ_step', 30, '\u00B0'),
    Form.Uniform('\U0001F526 Target luminence (0-100)', 'luminance_targ_per', 10, 100,'%'),
    Form.Constant('\U0001F526 Target luminence step size (0-100)', 'luminance_targ_step', 10,'%'),
    # For LG24GQ50B-B with height of 1080 pix and at 0.57 m distance conversion factor of 0.0259 deg/pix, 
    # the largest diameter of the screen area to display targets is int(1080 pix * 0.0259 deg/pix) = 27 deg
    Form.Uniform('\u2220 Min/Max ccentricity range for target locations', 'target_loc_eccentricity_deg', 1, 2, '\u00B0'),
    Form.Constant('Number of eccentricity steps for target locations', 'target_loc_eccentric_circle_num', 10),
    Form.Uniform('\u2220 Polar angle range for target locations - sector #1 (0..360)', 'target_loc_angle_sector1_deg', 0, 45, '\u00B0'),
    Form.Uniform('\u2220 Polar angle range for target locations - sector #2 (0..360)', 'target_loc_angle_sector2_deg', 45, 90, '\u00B0'),
    Form.Constant('\u2220 Polar angle step around the target location circle', 'target_loc_polar_step_deg', 30,'\u00B0'),
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
  reward_pertrial_ms_min_spinbox = form.findChild(QDoubleSpinBox, "reward_pertrial_ms_min")
  reward_pertrial_ms_min_spinbox.setRange(10, 500)
  reward_pertrial_ms_min_spinbox.setSingleStep(1)
  reward_pertrial_ms_max_spinbox = form.findChild(QDoubleSpinBox, "reward_pertrial_ms_max")
  reward_pertrial_ms_max_spinbox.setRange(100, 500)
  reward_pertrial_ms_max_spinbox.setSingleStep(1)
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
  target_loc_angle_sector1_deg_min_spinbox = form.findChild(QDoubleSpinBox, "target_loc_angle_sector1_deg_min")
  target_loc_angle_sector1_deg_min_spinbox.setRange(0, 360)
  target_loc_angle_sector1_deg_min_spinbox.setSingleStep(1)  
  target_loc_angle_sector1_deg_max_spinbox = form.findChild(QDoubleSpinBox, "target_loc_angle_sector1_deg_max")
  target_loc_angle_sector1_deg_max_spinbox.setRange(0, 360)
  target_loc_angle_sector1_deg_max_spinbox.setSingleStep(1) 
  target_loc_angle_sector2_deg_min_spinbox = form.findChild(QDoubleSpinBox, "target_loc_angle_sector2_deg_min")
  target_loc_angle_sector2_deg_min_spinbox.setRange(0, 360)
  target_loc_angle_sector2_deg_min_spinbox.setSingleStep(1)  
  target_loc_angle_sector2_deg_max_spinbox = form.findChild(QDoubleSpinBox, "target_loc_angle_sector2_deg_max")
  target_loc_angle_sector2_deg_max_spinbox.setRange(0, 360)
  target_loc_angle_sector2_deg_max_spinbox.setSingleStep(1) 
  target_loc_polar_step_deg_spinbox = form.findChild(QDoubleSpinBox, "target_loc_polar_step_deg")
  target_loc_polar_step_deg_spinbox.setRange(5, 180)
  target_loc_polar_step_deg_spinbox.setSingleStep(5) 
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

  return result

# === State Handlers (low-level, reusable) ===
async def acquire_fixation_func(context, get_gaze, center, accpt_gaze_radius_pix, monitorsubj_W_pix, monitorsubj_H_pix):
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

async def fixate1_func(context, get_gaze, center, accpt_gaze_radius_pix, duration, monitorsubj_W_pix, monitorsubj_H_pix):
    while True:
        success = await wait_for_hold(
            context,
            lambda: abs(QPoint.dotProduct(
                gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
                gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
            )) ** .5 < accpt_gaze_radius_pix,
            timedelta(seconds=duration),
            timedelta(seconds=0) # 0sec to ensure waiting indefinitely
        )

        if success:
            return
        
async def present_target_func(context, get_gaze, center, accpt_gaze_radius_pix, duration1, duration2, monitorsubj_W_pix, monitorsubj_H_pix):
  # Wait for the gaze to hold within fixation cross tolerances for the target presentation duration
  # if don't reaquire the target within blink_dur_ms, then ABORT the trial
  success = await wait_for_hold(
      context,
      lambda: abs(QPoint.dotProduct(
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
      )) ** .5 < accpt_gaze_radius_pix,
      timedelta(seconds=duration1), # target presentation duration
      timedelta(seconds=duration2) # allowed single blink duration
  )
  return success

async def fixate2_func(context, get_gaze, center, accpt_gaze_radius_pix, duration1, duration2, monitorsubj_W_pix, monitorsubj_H_pix):
  # Wait for the gaze to hold within the fixation window for the fix2 duration
  success = await wait_for_hold(
      context,
      lambda: abs(QPoint.dotProduct(
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
          gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
      )) ** .5 < accpt_gaze_radius_pix,
      timedelta(seconds=duration1),
      timedelta(seconds=duration2) # allowed single blink duration
  )
  return success

async def acquire_target_func(context, trial_type, get_gaze, center, targetpos_pix, accpt_gaze_radius_pix, duration1, duration2, monitorsubj_W_pix, monitorsubj_H_pix):
  if trial_type=="Delayed Saccade - Catch":
    success = await wait_for_hold(
        context,
        lambda: abs(QPoint.dotProduct(
            gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
            gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
        )) ** .5 < accpt_gaze_radius_pix,
        timedelta(seconds=duration1),
        timedelta(seconds=duration2) # allowed single blink duration
    )
  else: # trial_type=="Delayed Saccade - Photic" or trial_type=="Saccade - No Delay"
    # Wait for the gaze to move to the target position within the decision timeout
    success = await wait_for(
        context,
        lambda: abs(QPoint.dotProduct(
            gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - targetpos_pix,
            gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - targetpos_pix
        )) ** .5 < accpt_gaze_radius_pix,
        timedelta(seconds=duration1) # allowed single blink duration
    )
  return success

async def hold_target_func(context, trial_type, get_gaze, center, targetpos_pix, accpt_gaze_radius_pix, duration1, duration2, monitorsubj_W_pix, monitorsubj_H_pix):
  if trial_type=="Delayed Saccade - Catch":
    success = await wait_for_hold(
        context,
        lambda: abs(QPoint.dotProduct(
            gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center,
            gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - center
        )) ** .5 < accpt_gaze_radius_pix,
        timedelta(seconds=duration1),
        timedelta(seconds=duration2) # allowed single blink duration
    )
  else: # trial_type=="Delayed Saccade - Photic" or trial_type=="Saccade - No Delay"
  # Wait for the gaze to hold on the target position for the fix2 timeout
    success = await wait_for_hold(
        context,
        lambda: abs(QPoint.dotProduct(
            gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - targetpos_pix,
            gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix) - targetpos_pix
        )) ** .5 < accpt_gaze_radius_pix,
        timedelta(seconds=duration1), # target hold duration
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
    await context.log('BehavState=ACQUIRE_FIXATION_post-drawing')
    print(state)
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

async def handle_fixate1(
    context,
    get_gaze,
    center,
    accpt_gaze_radius_pix,
    fix1_duration,
    monitorsubj_W_pix,
    monitorsubj_H_pix,
    widget,
    converter
):
    global state
    state = State.FIXATE1
    await context.log('BehavState=FIXATE1_post-drawing')
    print(state)
    widget.update()
    await fixate1_func(context, get_gaze, center, accpt_gaze_radius_pix, fix1_duration, monitorsubj_W_pix, monitorsubj_H_pix)
    temp_gaze = gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix)
    await context.log(f"Gaze[X,Y]_pix-abs_after-FIXATE1={temp_gaze}")
    await context.log(f"Gaze[X,Y]_deg-abs_after-FIXATE1={converter.relpix_to_absdeg(temp_gaze.x(), temp_gaze.y())}")
    return

async def handle_present_target(
    context,
    trial_type,
    get_gaze,
    center,
    accpt_gaze_radius_pix,
    target_present_dur,
    blink_dur_ms,
    monitorsubj_W_pix,
    monitorsubj_H_pix,
    widget
):
    global state
    state = State.TARGET_PRESENTATION
    await context.log('BehavState=TARGET_PRESENTATION_post-drawing_PHOTODIODE-SQUARE')
    print(state)
    widget.update()
    if trial_type == "Saccade - No Delay": # if we want to move gaze to the target without it disappearing
      success = True # we don't need to wait for the gaze to hold on the target
    else:
      success = await present_target_func(context, get_gaze, center, accpt_gaze_radius_pix, target_present_dur, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix)
    return success

async def handle_fixate2(
    context,
    trial_type,
    get_gaze,
    center,
    accpt_gaze_radius_pix,
    fix2_duration,
    blink_dur_ms,
    monitorsubj_W_pix,
    monitorsubj_H_pix,
    widget
):
    global state
    state = State.FIXATE2
    await context.log('BehavState=FIXATE2')
    print(state)
    widget.update()
    if trial_type == "Saccade - No Delay": # if we want to move gaze to the target without it disappearing
      success = True # we don't need to wait for the gaze to hold on the target
    else:
      success = await fixate2_func(context, get_gaze, center, accpt_gaze_radius_pix, fix2_duration, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix)
    return success

async def handle_acquire_target(
    context,
    trial_type,
    get_gaze,
    center,
    targetpos_pix,
    accpt_gaze_radius_pix,
    decision_timeout,
    blink_dur_ms,
    monitorsubj_W_pix,
    monitorsubj_H_pix,
    widget,
    converter
):
    global state
    state = State.ACQUIRE_TARGET
    await context.log('BehavState=ACQUIRE_TARGET_start')
    print(state)
    widget.update()
    success = await acquire_target_func(context, trial_type, get_gaze, center, targetpos_pix, accpt_gaze_radius_pix, decision_timeout, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix)
    temp_gaze = gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix)
    await context.log(f"Gaze[X,Y]_pix-abs_after-acquiring-target={temp_gaze}")
    await context.log(f"Gaze[X,Y]_deg-abs_after-acquiring-target={converter.relpix_to_absdeg(temp_gaze.x(), temp_gaze.y())}")
    return success

async def handle_hold_target(
    context,
    trial_type,
    get_gaze,
    center,
    targetpos_pix,
    accpt_gaze_radius_pix,
    targethold_duration,
    blink_dur_ms,
    monitorsubj_W_pix,
    monitorsubj_H_pix,
    widget,
    converter
):
    global state
    state = State.HOLD_TARGET
    await context.log('BehavState=HOLD_TARGET_start')
    print(state)
    widget.update()
    success = await hold_target_func(context, trial_type, get_gaze, center, targetpos_pix, accpt_gaze_radius_pix, targethold_duration, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix)
    temp_gaze = gaze_valid(get_gaze(), monitorsubj_W_pix, monitorsubj_H_pix)
    await context.log(f"Gaze[X,Y]_pix-abs_after-holding-target={temp_gaze}")
    await context.log(f"Gaze[X,Y]_deg-abs_after-holding-target={converter.relpix_to_absdeg(temp_gaze.x(), temp_gaze.y())}")
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
  global converter, center, center_f, circle_radii, rand_pos_i, \
    trial_num, trial_photic_count, trial_photic_success_count, trial_catch_count, \
    trial_catch_success_count, drawn_objects, rand_pos, reward_total_released_ms, \
    gaze_success_store, gaze_failure_store, failure_sound, abort_sound, success_sound, \
    photodiode_blinking_square, photodiode_static_square, WATCHING, state, \
    target_loc_polar_step_deg, target_loc_angle_sector1_deg_min, \
    target_loc_angle_sector1_deg_max, target_loc_angle_sector2_deg_min, target_loc_angle_sector2_deg_max, \
    target_loc_eccentricity_pix_min, target_loc_eccentricity_pix_max, target_loc_eccentric_circle_num

  # Get the task configuration
  config = context.task_config
  monitorsubj_W_pix = config['monitorsubj_W_pix']
  monitorsubj_H_pix = config['monitorsubj_H_pix']
  monitorsubj_dist_m = config['monitorsubj_dist_m']
  monitorsubj_width_m = config['monitorsubj_width_m']

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

    # Adding sound files to the task
    current_directory = os.getcwd() # Get the current working directory
    # Define a relative path (e.g., accessing a file in a subdirectory)
    relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'failure_clip.wav')
    failure_sound = QSound(relative_path) # Load the .wav file (replace with your file path)
    relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'failure_clip.wav')
    # Sound file is called "failure" because in default tasks it's used for FAILURE, in VCP tasks it's used for ABORT
    abort_sound = QSound(relative_path) 
    relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'success_clip.wav')
    success_sound = QSound(relative_path) # Load the .wav file (replace with your file path)

    target_loc_polar_step_deg = config['target_loc_polar_step_deg']
    target_loc_angle_sector1_deg_min = config['target_loc_angle_sector1_deg']['min']
    target_loc_angle_sector1_deg_max = config['target_loc_angle_sector1_deg']['max']
    target_loc_angle_sector2_deg_min = config['target_loc_angle_sector2_deg']['min']
    target_loc_angle_sector2_deg_max = config['target_loc_angle_sector2_deg']['max']
    target_loc_eccentricity_pix_min = converter.deg_to_pixel_rel(config['target_loc_eccentricity_deg']['min'])
    target_loc_eccentricity_pix_max = converter.deg_to_pixel_rel(config['target_loc_eccentricity_deg']['max'])
    target_loc_eccentric_circle_num = config['target_loc_eccentric_circle_num'] # Number of concentric circles along which target locations will be generated
    
    circle_radii = np.linspace(target_loc_eccentricity_pix_min, target_loc_eccentricity_pix_max, \
                               target_loc_eccentric_circle_num) # Generate radii for concentric circles along which targets are displayed
    rand_pos_i = 0
    trial_num = 0

    valid_angles_deg = get_valid_angles(target_loc_polar_step_deg, target_loc_angle_sector1_deg_min,
        target_loc_angle_sector1_deg_max, target_loc_angle_sector2_deg_min, target_loc_angle_sector2_deg_max) # Get valid angles in degrees
    valid_angles_rad = [np.deg2rad(a) for a in valid_angles_deg] # Convert degrees to radians

    # Generate random positions around a circle
    rand_pos = [
      (center.x() + radius*np.cos(angle), center.y() - radius*np.sin(angle))
      for radius in circle_radii
      for angle in valid_angles_rad # Use pre-generated valid angles
    ]
    random.shuffle(rand_pos) # Shuffle the list of random positions to randomize their order
    
    # print("xxxxxxxxxxxxxxxxxxxxxxxxxrand_pos (pix)xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
    # print("xxxxxxxxxxxxxxxxxxxxxxxxxrand_pos (pix)xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
    # print(rand_pos)
    # print("xxxxxxxxxxxxxxxxxxxxxxxxxrand_pos (pix)xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
    # print("xxxxxxxxxxxxxxxxxxxxxxxxxrand_pos (pix)xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx") 

    # Create a QColor object for the photodiode blinking square and static square
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
        context.trial_summary_data.used_values['code_name_and_version'] = ["gaussian_delayed_saccade_task_code_v2.0"]

    if not WATCHING:
      context.config.add_recursive_observer(on_change) # this method is responsible for registering 
      # "on_change()" as a listener for changes. "on_change" will be executed automatically by the
      # add_recursive_observer() mechanism whenever the relevant event occurs
      WATCHING = True
 

  """ Below are commands that will be executed on every trial """

  # Regenerate target positions if user changes any of the parameters that affect target positions
  if int(target_loc_eccentric_circle_num) != int(config['target_loc_eccentric_circle_num']) or \
      int(target_loc_polar_step_deg) != int(config['target_loc_polar_step_deg']) or \
      int(target_loc_eccentricity_pix_min) != int(converter.deg_to_pixel_rel(config['target_loc_eccentricity_deg']['min'])) or \
      int(target_loc_eccentricity_pix_max) != int(converter.deg_to_pixel_rel(config['target_loc_eccentricity_deg']['max'])) or \
      int(target_loc_angle_sector1_deg_min) != int(config['target_loc_angle_sector1_deg']['min']) or \
      int(target_loc_angle_sector1_deg_max) != int(config['target_loc_angle_sector1_deg']['max']) or \
      int(target_loc_angle_sector2_deg_min) != int(config['target_loc_angle_sector2_deg']['min']) or \
      int(target_loc_angle_sector2_deg_max) != int(config['target_loc_angle_sector2_deg']['max']):
          print("xxxxxxxxxxxxxxxxxxxxxxxxxCHANGE FLAGxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
          print("xxxxxxxxxxxxxxxxxxxxxxxxxCHANGE FLAGxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
          print('detected change!!')
          print(f"step_deg_1={int(target_loc_polar_step_deg)}, \
           step_deg_2={int(config['target_loc_polar_step_deg'])}")
          print(f"angle_sect1_deg_min_1={int(target_loc_angle_sector1_deg_min)}, \
           angle_sect1_deg_min_2={int(config['target_loc_angle_sector1_deg']['min'])}")
          print(f"angle_sect1_deg_max_1={int(target_loc_angle_sector1_deg_max)}, \
           angle_sect1_deg_max_2={int(config['target_loc_angle_sector1_deg']['max'])}")
          print(f"angle_sect2_deg_min_1={int(target_loc_angle_sector2_deg_min)}, \
           angle_sect2_deg_min_2={int(config['target_loc_angle_sector2_deg']['min'])}")
          print(f"angle_sect2_deg_max_1={int(target_loc_angle_sector2_deg_max)}, \
           angle_sect2_deg_max_2={int(config['target_loc_angle_sector2_deg']['max'])}")
          print("xxxxxxxxxxxxxxxxxxxxxxxxxCHANGE FLAGxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
          print("xxxxxxxxxxxxxxxxxxxxxxxxxCHANGE FLAGxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx") 
          
          target_loc_polar_step_deg = config['target_loc_polar_step_deg']
          target_loc_angle_sector1_deg_min = config['target_loc_angle_sector1_deg']['min']
          target_loc_angle_sector1_deg_max = config['target_loc_angle_sector1_deg']['max']
          target_loc_angle_sector2_deg_min = config['target_loc_angle_sector2_deg']['min']
          target_loc_angle_sector2_deg_max = config['target_loc_angle_sector2_deg']['max']
          target_loc_eccentricity_pix_min = converter.deg_to_pixel_rel(config['target_loc_eccentricity_deg']['min'])
          target_loc_eccentricity_pix_max = converter.deg_to_pixel_rel(config['target_loc_eccentricity_deg']['max'])
          target_loc_eccentric_circle_num = config['target_loc_eccentric_circle_num'] # Number of concentric circles along which target locations will be generated
          
          circle_radii = np.linspace(target_loc_eccentricity_pix_min, target_loc_eccentricity_pix_max, \
                                    int(target_loc_eccentric_circle_num)) # Generate radii for concentric circles along which targets are displayed
          rand_pos_i = 0

          valid_angles_deg = get_valid_angles(target_loc_polar_step_deg, target_loc_angle_sector1_deg_min,
              target_loc_angle_sector1_deg_max, target_loc_angle_sector2_deg_min, target_loc_angle_sector2_deg_max) # Get valid angles in degrees
          valid_angles_rad = [np.deg2rad(a) for a in valid_angles_deg] # Convert degrees to radians

          # Generate random positions around a circle
          rand_pos = [
            (center.x() + radius*np.cos(angle), center.y() - radius*np.sin(angle)) # Note: Y is inverted in Qt, so we subtract from center.y()
            for radius in circle_radii
            for angle in valid_angles_rad # Use pre-generated valid angles
          ]
          random.shuffle(rand_pos) # Shuffle the list of random positions to randomize their order

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
  accpt_gaze_radius_deg = config['accpt_gaze_radius_deg']
  accpt_gaze_radius_pix = converter.deg_to_pixel_rel(accpt_gaze_radius_deg)
  is_height_locked = config['is_height_locked']
  target_doesnt_disappear = config['target_doesnt_disappear']
  paint_all_targets = config['paint_all_targets']
  target_color_rgb = config['target_color']
  background_color = config['background_color']
  trial_type = config['task_group']
  background_color_qt = QColor(background_color[0], background_color[1], background_color[2], 255)
  
  # Get various timeouts from the context (user GUI)
  decision_timeout = context.get_value('decision_timeout') / 1000 # dividing by 1000x to convert from ms to s
  fix1_duration = context.get_value('fix1_duration') / 1000
  if trial_type == "Saccade - No Delay":
    fix2_duration = 0
    target_present_dur = 0
  else: # trial_type == "Delayed Saccade - Photic" or "Delayed Saccade - Catch"
    fix2_duration = context.get_value('fix2_duration') / 1000
    target_present_dur = context.get_value('target_present_dur') / 1000
  targethold_duration = context.get_value('targethold_duration') / 1000
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
  luminance_targ_per = pick_random_value(config['luminance_targ_per']['min'], config['luminance_targ_per']['max'], config['luminance_targ_step'])
  context.trial_summary_data.used_values['luminance_targ_per'] = luminance_targ_per # this command based on 'get_value()' from 'task_context.py' aalows to add values to task_config['used_values']
  orientation_targ_ran = pick_random_value(config['orientation_targ_ran']['min'], config['orientation_targ_ran']['max'], config['orientation_targ_step'])
  context.trial_summary_data.used_values['orientation_targ_ran'] = orientation_targ_ran 
  width_targ_pix = converter.deg_to_pixel_rel(pick_random_value(config['width_targ_deg']['min'], config['width_targ_deg']['max'], config['widthtargdeg_step']))
  context.trial_summary_data.used_values['width_targ_pix'] = width_targ_pix 
  if is_height_locked:
    height_targ_pix = width_targ_pix
  else:
    height_targ_pix = converter.deg_to_pixel_rel(pick_random_value(config['height_targ_deg']['min'], config['height_targ_deg']['max'], config['heighttargdeg_step']))
  context.trial_summary_data.used_values['height_targ_pix'] = height_targ_pix 

  gaussian = gaussian_gradient(QPointF(0, 0), background_color_qt, width_targ_pix/2, 3, 255, luminance_targ_per)
  def draw_gaussian(painter: QPainter):
    if paint_all_targets:
      # region -- TESTING-TARGET-LOCATIONS: Drawing version that preserves every plotted Gaussian
      # Check if the current target position already exists in drawn_objects
      # position_exists = any(obj['position'] == targetpos_pix for obj in drawn_objects)
      # if not position_exists: # If the position does not exist, append the new object
      drawn_objects.append({
        'position': targetpos_pix,
        'orientation': orientation_targ_ran,
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
      painter.rotate(orientation_targ_ran) #Apply rotation to gaussian
      painter.scale(1, height_targ_pix/width_targ_pix) # Apply X,Y scaling to gaussian; initial diameter is equal to width/2, hence need to scale only Y
      painter.fillRect(int(-widget.width()/2), int(-widget.height()/2), int(widget.width()), int(widget.height()), gaussian)
      # painter.fillRect draws the gaussian into a giant rectangle centered at the upper left corner of the screen 
      # and the above lines will stretch, rotate (if uncommented), and translate the gaussian to the correct place.
      painter.restore()
      # endregion

  def draw_circle_sectors(painter, center_x, center_y, radius, sectors, color=QColor(0, 230, 230, 150)):
    """
    Draws sectors (arcs) of a circle.
    sectors: list of (start_angle_deg, end_angle_deg) tuples
    """
    rect = QRectF(center_x - radius, center_y - radius, 2 * radius, 2 * radius)
    pen = painter.pen()
    pen.setWidth(1)
    pen.setColor(color)
    painter.setPen(pen)
    for start_deg, end_deg in sectors:
        # Qt uses 1/16th degree units, and 0 degrees is at 3 o'clock, positive is counterclockwise
        span_deg = (end_deg - start_deg) % 360
        painter.drawArc(rect, int(start_deg * 16), int(span_deg * 16))  # negative for clockwise

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

  start = time.perf_counter() # Get the current time

  def renderer(painter: QPainter):
    global state
    nonlocal gaussian
    painter.fillRect(QRect(0, 0, 4000, 4000), background_color_qt) # QColor(128, 128, 128, 255); make the background of desired color
    painter.fillRect(int(widget.width() - 150), int(widget.height() - 150), 150, 150, photodiode_static_square) # background small square bottom-right

    # Clearing Operator View canvas
    if getattr(context.widget, 'do_clear', False):
      # print('DO CLEAR')
      gaze_failure_store.clear()
      gaze_success_store.clear()      
      context.widget.do_clear = False

    # photodiode_blinking_square = QColor(255, 255, 255, 255) # Create a QColor object with white color and transparency control (i.e. alpha)
    # painter.fillRect(int(widget.width() - 100), int(widget.height()-100), 100, 100, photodiode_blinking_square) # Keep uncommented if want a constant white square for the photo-diode

    # Draw the fixation cross and Gaussian based on the current state
    if state in (State.ACQUIRE_FIXATION, State.FIXATE1):
      # Acquiring and fixating on the fixation cross
      pen = painter.pen()
      pen.setWidth(2)
      # pen.setColor(Qt.GlobalColor.red)
      pen.setColor(QColor(255, 0, 0))
      painter.setPen(pen)
      # draw_gaussian(painter)
      painter.drawPath(cross)
      
      if paint_all_targets:
        # region -- TESTING-TARGET-LOCATIONS: drawing of the concentric circles and XY axes
        # Dynamically calculate the center of the window
        pen.setWidth(1)
        pen.setColor(QColor(0, 230, 230, 150))  # Set color to blue with 60% transparency (alpha = 150)
        painter.setPen(pen)
        center_x = int(converter.screen_pixels.width / 2)
        center_y = int(converter.screen_pixels.height / 2)
        # Draw the sectors showing the target location grid
        sectors = [
          (target_loc_angle_sector1_deg_min, target_loc_angle_sector1_deg_max),
          (target_loc_angle_sector2_deg_min, target_loc_angle_sector2_deg_max)
        ]
        for radius in circle_radii:
            draw_circle_sectors(painter, center_x, center_y, radius, sectors)
            # painter.drawEllipse(QPointF(center_x, center_y), radius, radius)
        # Draw angled lines at polar angle step defining target locations
        for angle in np.arange(0, 360, target_loc_polar_step_deg):
            in_any_sector = any(angle_in_sector(angle, start, end) for start, end in sectors)
            if in_any_sector:
              x = int(center_x + circle_radii[-1] * np.cos(np.radians(angle)))
              y = int(center_y - circle_radii[-1] * np.sin(np.radians(angle)))
              painter.drawLine(center_x, center_y, x, y)
        pen.setWidth(2)
        pen.setColor(QColor(255, 0, 0))
        painter.drawPath(cross)
        # # Draw the center lines
        # pen.setColor(QColor(0, 255, 255, 75))
        # painter.drawLine(center_x, int(center_y - target_loc_eccentricity_pix_max), center_x, int(center_y + target_loc_eccentricity_pix_max))  # Vertical line
        # painter.drawLine(int(center_x - target_loc_eccentricity_pix_max), center_y, int(center_x + target_loc_eccentricity_pix_max), center_y)  # Horizontal line
        # endregion
    elif state == State.FIXATE2:
      # Fixation after target presentation
      # await context.log('BehavState=FIXATE2_start') # saving any variables / data from code
      pen = painter.pen()
      pen.setWidth(2)
      pen.setColor(QColor(255, 0, 0))
      painter.setPen(pen)
      painter.drawPath(cross)
      if trial_type == "Saccade - No Delay": # if we want to move gaze to the target without it disappearing
        draw_gaussian(painter)

    elif state == State.TARGET_PRESENTATION:
      # await context.log('BehavState=TARGET_PRESENTATION_start') # saving any variables / data from code
      pen = painter.pen()
      pen.setWidth(2)
      pen.setColor(QColor(255, 0, 0))
      painter.setPen(pen)
      draw_gaussian(painter)
      painter.drawPath(cross)
      painter.fillRect(int(widget.width() - 100), int(widget.height()-100), 100, 100, photodiode_blinking_square) # photodiode white square presentation

    elif state in (State.HOLD_TARGET, State.ACQUIRE_TARGET):
      if trial_type == "Saccade - No Delay": # if we want to move gaze to the target without it disappearing
        draw_gaussian(painter)

    # elif state == State.HOLD_TARGET:
    #   painter.fillRect(int(widget.width() - 100), int(widget.height()-100), 100, 100, photodiode_blinking_square) # photodiode white square presentation

    # Draw the shadings around targets in the OPERATOR view indicating the areas where responses are accepted as correct
    with painter.masked(RenderOutput.OPERATOR): # using a context manager to temporarily change the drawing behavior of the painter object
      # A feature of the operator view is that you can draw stuff only for the operator into it. Anything in this 
      # with painter.masked(RenderOutput.OPERATOR) block will only appear in the operator view.
      
      painter.fillRect(QRect(0, 0, 450, 220), QColor(255, 255, 255, 255)) # background white rectangle in the OView to see text

      path = QPainterPath()
      path.addEllipse(targetpos_f, accpt_gaze_radius_pix, accpt_gaze_radius_pix)
      painter.fillPath(path, QColor(255, 255, 255, 128))
      path = QPainterPath()
      path.addEllipse(center_f, accpt_gaze_radius_pix, accpt_gaze_radius_pix)
      painter.fillPath(path, QColor(255, 255, 255, 128))

      # Drawing the gaze position as a continuously moving point
      color_rgba = QColor(138, 43, 226, 255) # purple
      draw_gaze(painter, gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), color_rgba)

      # Drawing text message on the operator view
      # drawText(painter, "(0, 0)", QPoint(0, 0)) # Draw the text message
      # temp_calc = lambda: QPoint.dotProduct(gaze - center, gaze - center)**.5 < accpt_gaze_radius_pix
      # drawn_text = f"(Diff={temp_calc()}, acpt={QPoint.dotProduct(gaze - center, gaze - center)**.5})"
      drawText(painter, str(state), QPoint(0, 30), background_color_qt) # Draw the text message
      drawText(painter, f"TRIAL_NUM={trial_num}", QPoint(0, 60), background_color_qt) # Draw the text message
      drawText(painter, f"PHOTIC_TRIAL_SUCCESS = {trial_photic_success_count} / {trial_photic_count}", QPoint(0, 90), background_color_qt) # Draw the text message
      drawText(painter, f"CATCH_TRIAL_SUCCESS = {trial_catch_success_count} / {trial_catch_count}", QPoint(0, 120), background_color_qt) # Draw the text message
      drawText(painter, f"Total reward = {round(reward_total_released_ms)} ms", QPoint(0, 150), background_color_qt) # Draw the text message
      temp_gaze = gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix)
      drawn_text = f"({temp_gaze.x()}, {temp_gaze.y()})"
      drawText(painter, drawn_text, temp_gaze, background_color_qt) # Draw the text message
      drawText(painter, f"Gaze (pix): x = {temp_gaze.x()}, y = {temp_gaze.y()}", QPoint(0, 180), background_color_qt)

      # Drawing all previously painted gazes of failed target holding
      # for gaze_qpoint, color_rgba in gaze_failure_store:
      #   draw_gaze(painter, gaze_qpoint, color_rgba)

      # Drawing all previously painted gazes of successful target holding
      for gaze_qpoint, color_rgba in gaze_success_store:
        draw_gaze(painter, gaze_qpoint, color_rgba)

  # Set the renderer function to the widget's renderer
  widget.renderer = renderer
  # Store the context information into .tha file
  await context.log(json.dumps(context.config)) 

  # Plotting of variables using Thalamus' pipeline QT plots
  if trial_photic_count == 0: # to avoid division by 0 error
    photic_success_rate = 0
  else:
    photic_success_rate = trial_photic_success_count/trial_photic_count*100
  if trial_catch_count == 0: # to avoid division by 0 error
    catch_success_rate = 0
  else:
    catch_success_rate = trial_catch_success_count/trial_catch_count*100
  create_task_with_exc_handling(context.inject_analog('performance', AnalogResponse(
    data = [float(trial_num), photic_success_rate, catch_success_rate],
    spans=[Span(begin=0, end=1, name='Trial Number'), Span(begin=1, end=2, name='Photic trial success rate (%)'), \
            Span(begin=2, end=3, name='Catch trial success rate (%)')], sample_intervals=[0, 0, 0]
  )))
  await context.log(f"TRIAL_NUM={trial_num}, PHOTIC_TRIAL_SUCCESS_COUNT={trial_photic_success_count}, \
                    PHOTIC_TRIAL_NUM={trial_photic_count}, CATCH_TRIAL_SUCCESS_COUNT={trial_catch_success_count}, \
                    CATCH_TRIAL_NUM={trial_catch_count}") # saving any variables / data from code

  # Handles State.ACQUIRE_FIXATION
  await handle_acquire_fixation(context, lambda: gaze, center, accpt_gaze_radius_pix, monitorsubj_W_pix, monitorsubj_H_pix)

  # Handles State.FIXATE1
  await handle_fixate1(context, lambda: gaze, center, accpt_gaze_radius_pix, fix1_duration, monitorsubj_W_pix, monitorsubj_H_pix, widget, converter)

  # Handles State.TARGET_PRESENTATION
  success = await handle_present_target(context, trial_type, lambda: gaze, center, accpt_gaze_radius_pix, target_present_dur, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix, widget)
  
  # trial counters before the 1st "return" statement
  trial_num += 1 # Increment the trial counter
  if trial_type == "Delayed Saccade - Photic":
    trial_photic_count += 1 # Increment the photic trial counter
  elif trial_type == "Delayed Saccade - Catch":
    trial_catch_count += 1 # Increment the catch trial counter
  print(f"Started trial # {trial_num}, trial type = {trial_type}")
  await context.log(f"StartedTRIAL_NUM={trial_num}") # saving any variables / data from code

  if not success:
    await context.log('TrialResult=ABORT') # saving any variables / data from code
    state = State.ABORT
    print(state)
    abort_sound.play() # add ABORT sound
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  # Handles State.FIXATE2
  success = await handle_fixate2(context, trial_type, lambda: gaze, center, accpt_gaze_radius_pix, fix2_duration, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix, widget)
  # state = State.FIXATE2
  # await context.log('BehavState=FIXATE2') # saving any variables / data from code
  # print(state)
  # widget.update()
  # success = await fixate2_func(context, lambda: gaze, center, accpt_gaze_radius_pix, fix2_duration, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix)

  if not success:
    await context.log('TrialResult=ABORT') # saving any variables / data from code
    state = State.ABORT
    print(state)
    abort_sound.play() # add ABORT sound
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  # Handles State.ACQUIRE_TARGET
  success = await handle_acquire_target(context, trial_type, lambda: gaze, center, targetpos_pix, accpt_gaze_radius_pix, decision_timeout, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix, widget, converter)

  if not success:
    gaze_failure_store.append((gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), QColor(255, 69, 0, 128)))
    await context.log('TrialResult=FAILURE') # saving any variables / data from code
    state = State.FAILURE
    print(state)
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  # Handles State.HOLD_TARGET
  success = await handle_hold_target(context, trial_type, lambda: gaze, center, targetpos_pix, accpt_gaze_radius_pix, targethold_duration, blink_dur_ms, monitorsubj_W_pix, monitorsubj_H_pix, widget, converter)

  if not success:
    gaze_failure_store.append((gaze_valid(gaze, monitorsubj_W_pix, monitorsubj_H_pix), QColor(255, 69, 0, 128)))
    await context.log('TrialResult=FAILURE') # saving any variables / data from code
    state = State.FAILURE
    print(state)
    # widget.update() # DC added
    failure_sound.play()
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

  if trial_type == "Delayed Saccade - Photic":
    trial_photic_success_count += 1
  if trial_type == "Delayed Saccade - Catch":
    trial_catch_success_count += 1
  # "TaskResult" is used to determine whether the trial is or is not removed from the queue
  # If TaskResult(False), the trial is not removed from the queue. If TaskResult(True), the trial is removed from the queue.
  return TaskResult(False)


