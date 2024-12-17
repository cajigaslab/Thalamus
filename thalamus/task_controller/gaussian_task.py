"""
Implementation of the Gaussian delayed saccade task
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
  ('fail_timeout', timedelta),
  ('decision_timeout', timedelta),
  ('fix1_timeout', timedelta),
  ('fix2_timeout', timedelta),
  ('blink_timeout', timedelta),
  ('success_timeout', timedelta),
  ('penalty_delay', timedelta),
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
    Form.Choice('Shape', 'shape', list(zip(shapes, shapes)))  # Add the shape attribute
  )
  layout.addWidget(form)

  # spinbox allows to constraint value options for above constants
  width_spinbox = form.findChild(QDoubleSpinBox, "width")
  width_spinbox.setRange(.1, 10.0)
  width_spinbox.setSingleStep(.1)
  height_spinbox = form.findChild(QDoubleSpinBox, "height")
  height_spinbox.setRange(.1, 10.0)
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
FRAMERATE = 60
INTERVAL = 1/FRAMERATE

class Size(typing.NamedTuple):
  width: int
  height: int

class Converter:
  def __init__(self, screen_pixels: Size, screen_width_m: float, screen_distance_m: float):
    self.screen_pixels = screen_pixels
    self.screen_width_m = screen_width_m
    self.screen_distance_m = screen_distance_m
    self.screen_width_rad = 2*np.arctan2(screen_width_m/2, screen_distance_m)
    self.rad_per_pixel = self.screen_width_rad/screen_pixels.width
    self.deg_per_pixel = 180/np.pi*self.rad_per_pixel
    self.m_per_pixel = screen_width_m/screen_pixels.width

  def deg_to_pixel_abs(self, *args) -> typing.Tuple[int, int]:
    result = self.deg_to_pixel_rel(*args)
    if len(result) == 2:
      return result[0] + self.screen_pixels.width/2, result[1] + self.screen_pixels.height/2,
    else:
      return result[0] + self.screen_pixels.width/2

  def deg_to_pixel_rel(self, *args) -> typing.Tuple[int, int]:
    if len(args) == 1:
      if isinstance(args[0], numbers.Number):
        return args[0]/self.deg_per_pixel
      else:
        x, y = args[0][0], args[0][1]
    else:
      x, y = args[0], args[1]

    return x/self.deg_per_pixel, y/self.deg_per_pixel

def gaussian_gradient(center: QPointF, radius: float, deviations: float = 1):
  gradient = QRadialGradient(center, radius)
  resolution = 1000
  for i in range(resolution+1):
    level = int(255*np.exp(-(deviations*i/resolution)**2/(2)))
    if level < 10:
      level = 0
    gradient.setColorAt(i/resolution, QColor(level, level, level))
  return gradient

class State(enum.Enum):
  ACQUIRE_FIXATION = enum.auto()
  FIXATE_1 = enum.auto()
  TARGET_PRESENTATION = enum.auto()
  FIXATE_2 = enum.auto()
  ACQUIRE_TARGET = enum.auto()
  HOLD_TARGET = enum.auto()
  SUCCESS = enum.auto()
  FAILURE = enum.auto()

converter = Converter(Size(1920, 1200), .5283, .57)
center = QPoint(converter.screen_pixels.width, converter.screen_pixels.height)/2
center_f = QPointF(float(converter.screen_pixels.width), float(converter.screen_pixels.height))/2
num_circles = 10
circle_radii = np.linspace(0, converter.screen_pixels.height, 10)
circle_radii += converter.screen_pixels.width/num_circles
circle_radii /= 2
rand_pos_i = 0
rand_pos = [
  (center.x() + radius*np.cos(angle), center.y() + radius*np.sin(angle))
  for radius in circle_radii
  for angle in np.arange(0, 2*np.pi, np.pi/6)
]
random.shuffle(rand_pos)
WINDOW_DEG = 2
WINDOW_PIX = converter.deg_to_pixel_rel(WINDOW_DEG)

@animate(60)
async def run(context: TaskContextProtocol) -> TaskResult: #pylint: disable=too-many-statements
  """
  Implementation of the state machine for the simple task
  """

  """
  Below is an object that contains a realization generated by sampling from the random
  distributions defined in the task_config. It itself has no logic, it simply holds
  the realization's values.
  """
  global rand_pos_i, rand_pos

  current_directory = os.getcwd() # Get the current working directory
  # Define a relative path (e.g., accessing a file in a subdirectory)
  relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'failure_clip.wav')
  failure_sound = QSound(relative_path) # Load the .wav file (replace with your file path)
  relative_path = os.path.join(current_directory, 'thalamus\\task_controller', 'success_clip.wav')
  success_sound = QSound(relative_path) # Load the .wav file (replace with your file path)

  if rand_pos_i == len(rand_pos):
    random.shuffle(rand_pos)
    rand_pos_i = 0
  target_pos = QPoint(rand_pos[rand_pos_i][0], rand_pos[rand_pos_i][1])
  target_pos_f = QPointF(rand_pos[rand_pos_i][0], rand_pos[rand_pos_i][1])
  current_rand_pos_i = rand_pos_i
  rand_pos_i += 1

  vertices_deg = [ # Define the vertices for the fixation cross
      (-0.25, 0), (0.25, 0),  # Horizontal line
      (0, -0.25), (0, 0.25)  # Vertical line
  ]
  vertices = [converter.deg_to_pixel_abs(p) for p in vertices_deg]
  cross = QPainterPath()
  cross.moveTo(vertices[0][0], vertices[0][1])
  cross.lineTo(vertices[1][0], vertices[1][1])
  cross.moveTo(vertices[2][0], vertices[2][1])
  cross.lineTo(vertices[3][0], vertices[3][1])

  square_size_deg = .5
  square_size = converter.deg_to_pixel_rel(square_size_deg)

  config = context.task_config
  width, height = config['width'], config['height']
  width = converter.deg_to_pixel_rel(width)
  height = converter.deg_to_pixel_rel(height)
  orientation, opacity = config['orientation'], config['opacity']
  is_height_locked = config['is_height_locked']
  target_color_rgb = config['target_color']
  
  blink_timeout = context.get_value('blink_timeout')
  decision_timeout = context.get_value('decision_timeout')
  fix1_timeout = context.get_value('fix1_timeout')
  fix2_timeout = context.get_value('fix2_timeout')
  penalty_delay = context.get_value('penalty_delay')
  if is_height_locked:
      height = width

  gaussian = gaussian_gradient(QPointF(0, 0), width/2, 3)
  def draw_gaussian(painter: QPainter):
    painter.save()
    painter.translate(target_pos)
    #Apply rotation to gaussian
    #painter.rotate(45)
    #Apply X,Y scaling to gaussian
    #painter.scale(2,1)
    painter.fillRect(-widget.width()/2, -widget.height()/2, widget.width(), widget.height(), gaussian)
    painter.restore()


  state = State.ACQUIRE_FIXATION
  widget: CanvasProtocol = context.widget
  assert widget is not None

  gaze = QPoint(0,0)
  def gaze_handler(cursor: QPoint) -> None:
    nonlocal gaze
    gaze = cursor

  widget.gaze_listener = gaze_handler

  start = time.perf_counter()
  def renderer(painter: QPainter):
    nonlocal gaussian
    create_task_with_exc_handling(context.inject_analog('Node 1', AnalogResponse(
      data = [np.sin(time.perf_counter() - start), float(current_rand_pos_i)],
      spans=[Span(begin=0, end=1, name='Sin'), Span(begin=1, end=2, name='Trial Number')],
      sample_intervals=[0, 0]
    )))

    if state in (State.ACQUIRE_FIXATION, State.FIXATE_1):
      pen = painter.pen()
      pen.setWidth(3)
      pen.setColor(Qt.GlobalColor.red)
      painter.setPen(pen)
      draw_gaussian(painter)
      painter.drawPath(cross)
    elif state == State.TARGET_PRESENTATION:
      pen = painter.pen()
      pen.setWidth(3)
      pen.setColor(Qt.GlobalColor.red)
      painter.setPen(pen)
      draw_gaussian(painter)
      painter.drawPath(cross)
      painter.fillRect(widget.width() - 100, widget.height()-100, 100, 100, Qt.GlobalColor.white)
    elif state == State.FIXATE_2:
      pen = painter.pen()
      pen.setWidth(3)
      pen.setColor(Qt.GlobalColor.red)
      painter.setPen(pen)
      painter.drawPath(cross)

    with painter.masked(RenderOutput.OPERATOR):
      path = QPainterPath()
      path.addEllipse(target_pos_f, WINDOW_PIX, WINDOW_PIX)
      painter.fillPath(path, QColor(255, 255, 255, 128))
      path = QPainterPath()
      path.addEllipse(center_f, WINDOW_PIX, WINDOW_PIX)
      painter.fillPath(path, QColor(255, 255, 255, 128))

  widget.renderer = renderer
  await context.log(json.dumps(context.config))

  print(state)
  acquired = False
  while not acquired:
    print(state)
    acquired = await wait_for(context, lambda: QPoint.dotProduct(gaze - center, gaze-center)**.5 < WINDOW_PIX, timedelta(seconds=1))

  state = State.FIXATE_1
  print(state)
  widget.update()
  await wait_for_hold(context, lambda: QPoint.dotProduct(gaze - center, gaze-center)**.5 < WINDOW_PIX, timedelta(seconds=fix1_timeout), timedelta(seconds=-1))

  state = State.TARGET_PRESENTATION
  print(state)
  widget.update()
  success = await wait_for_hold(context, lambda: QPoint.dotProduct(gaze - center, gaze-center)**.5 < WINDOW_PIX, timedelta(seconds=blink_timeout), timedelta(seconds=.1))
  if not success:
    state = State.FAILURE
    print(state)
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  state = State.FIXATE_2
  print(state)
  widget.update()
  success = await wait_for_hold(context, lambda: QPoint.dotProduct(gaze - center, gaze-center)**.5 < WINDOW_PIX, timedelta(seconds=fix2_timeout), timedelta(seconds=.1))
  if not success:
    state = State.FAILURE
    print(state)
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  state = State.ACQUIRE_TARGET
  print(state)
  widget.update()
  success = await wait_for(context, lambda: QPoint.dotProduct(gaze - target_pos, gaze-target_pos)**.5 < WINDOW_PIX, timedelta(seconds=decision_timeout))
  if not success:
    state = State.FAILURE
    print(state)
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  state = State.HOLD_TARGET
  print(state)
  widget.update()
  success = await wait_for_hold(context, lambda: QPoint.dotProduct(gaze - target_pos, gaze-target_pos)**.5 < WINDOW_PIX,
                                timedelta(seconds=fix2_timeout), timedelta(seconds=.1))
  if not success:
    state = State.FAILURE
    print(state)
    failure_sound.play()
    await context.sleep(timedelta(seconds=penalty_delay))
    return TaskResult(False)

  state = State.SUCCESS
  print(state)
  success_sound.play()

  return TaskResult(False)


