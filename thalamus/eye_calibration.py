import re
import sys
import asyncio
import typing
import json

import grpc

import numpy

from thalamus.qt import *
from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc
from thalamus.task_controller.util import RenderOutput, create_task_with_exc_handling

from thalamus.util import IterableQueue

from thalamus.thread import ThalamusThread
print(thalamus_pb2.__file__)

POINT_SIZE = 10
QUAD_TO_LABEL = {
  (True, True): 'IV',
  (True, False): 'II',
  (False, True): 'III',
  (False, False): 'I',
}

DEFAULT_PROJECTIVE = {
  'Parameters': [0.5, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0],
  'Distance (m)': 1.0,
  'DPI': 100.0,
}

DEFAULT_ANGULAR_SCALING = {
  'Angle': [],
  'Scale X': [],
  'Scale Y': [],
}

class SaccadeTarget(typing.NamedTuple):
  x: int
  y: int

class Task:
  def __init__(self, stub: thalamus_pb2_grpc.ThalamusStub, config: dict):
    self.stub = stub
    self.oculomatic_task: asyncio.Task | None = None
    self.task: asyncio.Task | None = None
    self.show_fixation = False
    self.show_saccade = False
    self.fixation_radius = 50
    self.saccade_radius = 100
    self.current_saccade = 0
    self.reward_ms = 500
    self.saccades: list[SaccadeTarget] = []
    self.path = QPainterPath()
    self.training_path = QPainterPath()
    self.points = []
    self.reward_queue = IterableQueue()
    self.distance_m = 1.0
    self.dpi = 1.0
    self.model_name = 'Projective'
    self.config = config
    self.training_data = []
    self.nudge_index = -1
    self.nudge_start_value = None

    eye_scaling = config['eye_scaling']
    models = None
    projective = None
    def on_change(source, action, key, value):
      nonlocal models, projective

      if source == eye_scaling:
        if key == 'Models':
          models = value
          value.recap()
        elif key == 'Selected Model':
          self.model_name = value
          self.rebuild()
      elif source == models:
        if key == 'Projective':
          projective = value
          value.recap()
      elif source == projective:
        if key == 'Distance (m)':
          self.distance_m = value
          self.rebuild()
        elif key == 'DPI':
          self.dpi = value
          self.rebuild()
      #print(key, value)
      #print('params', self.distance_m, self.dpi)

    eye_scaling.add_recursive_observer(on_change)
    eye_scaling.recap()

  async def prepare_reward(self):
    self.stub.inject_analog(self.reward_queue)
    await self.reward_queue.put(thalamus_pb2.InjectAnalogRequest(node='Reward'))

  async def deliver_reward(self):
    await self.reward_queue.put(thalamus_pb2.InjectAnalogRequest(signal=thalamus_pb2.AnalogResponse(
      data=[5,0],
      spans=[thalamus_pb2.Span(begin=0,end=2,name='Reward')],
      sample_intervals=[1_000_000*self.reward_ms])))

  def render(self, painter: QPainter, size: QSize, output: RenderOutput):
    painter.setBrush(Qt.GlobalColor.black)
    painter.drawRect(0, 0, size.width(), size.height())
    
    fixation_color = QColor(Qt.GlobalColor.red)
    fixation_shape = QRect(size.width()//2 - self.fixation_radius, size.height()//2 - self.fixation_radius,
                           2*self.fixation_radius, 2*self.fixation_radius)
    
    saccade_color = QColor(Qt.GlobalColor.white)
    saccade_shape = QRect(0, 0,
                           2*self.saccade_radius, 2*self.saccade_radius)
    if output == RenderOutput.OPERATOR:
      painter.save()
      painter.setPen(Qt.GlobalColor.yellow)
      painter.drawLine(0, size.height()//2, size.width(), size.height()//2)
      painter.drawLine(size.width()//2, 0, size.width()//2, size.height())
      painter.restore()

      if not self.show_fixation:
        fixation_color.setAlpha(64)

      painter.save()
      pen = painter.pen()
      pen.setColor(fixation_color)
      pen.setWidth(5)
      painter.setPen(pen)
      painter.drawLine(fixation_shape.left(), fixation_shape.center().y(), fixation_shape.right(), fixation_shape.center().y())
      painter.drawLine(fixation_shape.center().x(), fixation_shape.top(), fixation_shape.center().x(), fixation_shape.bottom())
      painter.restore()
      #painter.drawEllipse(fixation_shape)

      for i, saccade in enumerate(self.saccades):
        if self.show_saccade and i == self.current_saccade:
          saccade_color.setAlpha(255)
        else:
          saccade_color.setAlpha(64)

        painter.save()
        painter.setBrush(saccade_color)
        if i == self.current_saccade:
          painter.setPen(Qt.GlobalColor.green)
        painter.drawEllipse(saccade_shape.adjusted(saccade.x - self.saccade_radius + size.width()//2,
                                                   saccade.y - self.saccade_radius + size.height()//2,
                                                   saccade.x - self.saccade_radius + size.width()//2,
                                                   saccade.y - self.saccade_radius + size.height()//2))
        painter.restore()

      #Draw all oculomatic data
      painter.save()
      pen = painter.pen()
      pen.setCosmetic(True)
      pen.setWidth(POINT_SIZE)
      pen.setColor(Qt.GlobalColor.blue)
      pen.setCapStyle(Qt.PenCapStyle.RoundCap)
      painter.setPen(pen)
      painter.translate(size.width()//2, size.height()//2)
      painter.drawPath(self.path)
      pen = painter.pen()
      pen.setWidth(POINT_SIZE)
      pen.setColor(Qt.GlobalColor.red)
      painter.setPen(pen)
      painter.drawPath(self.training_path)
      painter.restore()
      
    elif output == RenderOutput.SUBJECT:
      if self.show_fixation:
        painter.save()
        pen = painter.pen()
        pen.setColor(fixation_color)
        pen.setWidth(5)
        painter.setPen(pen)
        painter.drawLine(fixation_shape.left(), fixation_shape.center().y(), fixation_shape.right(), fixation_shape.center().y())
        painter.drawLine(fixation_shape.center().x(), fixation_shape.top(), fixation_shape.center().x(), fixation_shape.bottom())
        painter.restore()

      if self.saccades and self.show_saccade:
        painter.setBrush(saccade_color)
        saccade = self.saccades[self.current_saccade]
        painter.drawEllipse(saccade_shape.adjusted(saccade.x - self.saccade_radius + size.width()//2,
                                                   saccade.y - self.saccade_radius + size.height()//2,
                                                   saccade.x - self.saccade_radius + size.width()//2,
                                                   saccade.y - self.saccade_radius + size.height()//2))

  def stop(self):
    self.task.cancel()

  def start(self):
    self.task = asyncio.create_task(self.run())

  def add_target(self, x: int, y: int):
    self.saccades.append(SaccadeTarget(x, y))
    print(self.saccades)

  def remove_target(self, x: int, y: int):
    for i, target in list(enumerate(self.saccades))[::-1]:
      distance = (target.x - x)**2 + (target.y - y)**2
      print(distance)
      if distance < self.saccade_radius**2:
        del self.saccades[i]
        if i <= self.current_saccade:
          self.current_saccade -= 1
        break

  def get_model(self, name: str):
    models = self.config['eye_scaling']['Models']
    if name not in models:
      if name == 'Projective':
        models[name] = DEFAULT_PROJECTIVE
        return DEFAULT_PROJECTIVE
      elif name == 'Angular Scaling':
        models[name] = DEFAULT_ANGULAR_SCALING
        return DEFAULT_ANGULAR_SCALING
    return models.get(name, None)

  def oculomatic_to_pixels_from_center(self, x, y):
    model = self.get_model(self.model_name)
    if self.model_name == 'Projective':
      #print(model)
      a, b, c, d, e, f, g, h = model['Parameters']
      #compute oculomatic -> angles
      tx = (a*x + b*y + c) / (g*x + h*y + 1)
      ty = (d*x + e*y + f) / (g*x + h*y + 1)

      #compute angles -> pixels
      distance_cm = 100*self.distance_m
      distance_inches = distance_cm/2.54
      px = numpy.tan(tx*numpy.pi/180)*distance_inches*self.dpi
      py = numpy.tan(ty*numpy.pi/180)*distance_inches*self.dpi
      return px, py
    elif self.model_name == 'Angular Scaling':
      angle, scalex, scaley = model['Angle'], model['Scale X'], model['Scale Y']
      length = min(len(angle), len(scalex), len(scaley))
      if not length:
        return 10*x, 10*y
      
      val = numpy.arctan2(y, x)
      if val < 0:
        val = numpy.pi + (numpy.pi + val)
      factorx = numpy.interp(val, angle[:length], scalex[:length], period=2*numpy.pi)
      factory = numpy.interp(val, angle[:length], scaley[:length], period=2*numpy.pi)
      return factorx*x, factory*y

    
  def append_to_path(self, path, x, y):
    #Move to px, py, then draw an extremely short line.  With Round pen caps the result should be a circle the
    #size of the pen width.  If the pen is set to cosmetic then the circle size won't change with scale factors.
    path.moveTo(QPointF(x, y))
    path.lineTo(QPointF(x+1e-6, y))

  def add_training_sample(self, x, y, saccade_index: int):
    print('saccade_index', saccade_index)
    if saccade_index < 0 or saccade_index >= len(self.saccades):
      return
    
    min_distance, min_index = numpy.inf, -1
    for i, p in enumerate(self.points):
      px, py = self.oculomatic_to_pixels_from_center(p[0], p[1])
      distance = (px - x)**2 + (py - y)**2
      if distance < min_distance:
        min_distance = distance
        min_index = i

    if min_index == -1:
      return
    
    p = self.points[min_index]
    px, py = self.oculomatic_to_pixels_from_center(p[0], p[1])
    target = self.saccades[saccade_index]

    #target distance from center in meters
    from_center_m = target.x/self.dpi*2.54/100, target.y/self.dpi*2.54/100

    degrees = (180/numpy.pi*numpy.arctan2(from_center_m[0], self.distance_m),
               180/numpy.pi*numpy.arctan2(from_center_m[1], self.distance_m))

    self.append_to_path(self.training_path, px, py)
    self.training_path.lineTo(target.x, target.y)
    self.training_data.append((self.points[min_index], degrees, target))

  def pop_training_sample(self):
    self.training_data.pop()
    self.rebuild()

  def rebuild(self):
    self.path.clear()
    self.training_path.clear()
    if self.get_model(self.model_name) is None:
      return

    for x, y in self.points:
      px, py = self.oculomatic_to_pixels_from_center(x, y)
      self.append_to_path(self.path, px, py)

    for p, _, target in self.training_data:
      px, py = self.oculomatic_to_pixels_from_center(p[0], p[1])
      self.append_to_path(self.training_path, px, py)
      if ((target.x - px)**2 + (target.y - py)**2)**.5 > 1:
        self.training_path.lineTo(target.x, target.y)

  def reset(self):
    model = self.get_model(self.model_name)
    print('reset', self.model_name, model)
    if self.model_name == 'Projective':
      model.assign(DEFAULT_PROJECTIVE)
      pass
    elif self.model_name == 'Angular Scaling':
      model['Angle'].assign([], self.rebuild)
      model['Scale X'].assign([], self.rebuild)
      model['Scale Y'].assign([], self.rebuild)

  def start_nudge(self, start_point: typing.Tuple[int, int]):
    model = self.get_model(self.model_name)
    if self.model_name != 'Angular Scaling':
      return

    sx, sy = start_point

    length = min(len(model['Angle']), len(model['Scale X']), len(model['Scale Y']))
    
    min_distance, min_index = numpy.inf, -1
    for i in range(length):
      model_angle, scalex, scaley = model['Angle'][i], model['Scale X'][i], model['Scale Y'][i]

      start_angle = numpy.arctan2(sy/scaley, sx/scalex)
      if start_angle < 0:
        start_angle = numpy.pi + (numpy.pi + start_angle)

      distance = abs(model_angle - start_angle)
      if distance < min_distance:
        min_distance = distance
        min_index = i

    if min_index == -1:
      return

    self.nudge_index = min_index
    self.nudge_start_value = model['Scale X'][self.nudge_index], model['Scale Y'][self.nudge_index]

  def stop_nudge(self):
    self.nudge_index = -1

  def nudge(self, start_point: typing.Tuple[int, int], current_point: typing.Tuple[int, int]):
    #print('self.current_saccade', self.current_saccade)
    if self.nudge_index == -1:
      return

    sx, sy = start_point
    cx, cy = current_point
    xscale, yscale = cx/sx, cy/sy

    model = self.get_model(self.model_name)
    if self.model_name == 'Angular Scaling':
      model['Scale X'].setitem(self.nudge_index, self.nudge_start_value[0]*xscale, self.rebuild)
      model['Scale Y'].setitem(self.nudge_index, self.nudge_start_value[1]*yscale, self.rebuild)

  def optimize(self):
    model = self.get_model(self.model_name)
    if self.model_name == 'Projective':
      if not self.training_data:
        return
      
      # Fit projective
      n = len(self.training_data)
      A = numpy.zeros((2*n, 8))
      b = numpy.zeros(2*n)
      for i in range(n):
          x, y = self.training_data[i][0]
          tx, ty = self.training_data[i][1]
          A[2*i]   = [x, y, 1, 0, 0, 0, -tx*x, -tx*y]
          b[2*i]   = tx
          A[2*i+1] = [0, 0, 0, x, y, 1, -ty*x, -ty*y]
          b[2*i+1] = ty
      params, _, _, _ = numpy.linalg.lstsq(A, b, rcond=None)
      model['Parameters'].assign(params, self.rebuild)
    elif self.model_name == 'Angular Scaling':
      if not self.training_data:
        return
      
      new_angles = []
      new_scalesx = []
      new_scalesy = []
      for ocu, deg, target in self.training_data:
        angle = numpy.arctan2(ocu[1], ocu[0])
        if angle < 0:
          angle = numpy.pi + (numpy.pi + angle)
        scalex = target.x/ocu[0]
        scaley = target.y/ocu[1]

        new_angles.append(angle)
        new_scalesx.append(scalex)
        new_scalesy.append(scaley)
      model['Angle'].assign(new_angles, self.rebuild)
      model['Scale X'].assign(new_scalesx, self.rebuild)
      model['Scale Y'].assign(new_scalesy, self.rebuild)

  async def run(self):
    async def eye_loop():
      eye_stream = self.stub.analog(thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(type='OCULOMATIC')))
      async for m in eye_stream:
        x, y = None, None
        for span in m.spans:
          if span.name == 'X':
            x = m.data[span.begin]
          elif span.name == 'Y':
            y = m.data[span.begin]

        if x is None or y is None:
          continue

        y *= -1
        self.points.append((x, y))
        p = self.oculomatic_to_pixels_from_center(x, y)
        if p is None:
          continue
        px, py = p
        self.append_to_path(self.path, px, py)

    await eye_loop()
    #await asyncio.gather(eye_loop(), state_loop(self.stub), main_loop())


class SubjectView(QWidget):
  def __init__(self, task: Task):
    super().__init__()
    self.setWindowTitle('Subject')
    self.task = task

  def paintEvent(self, e):
    painter = QPainter(self)
    try:
      self.task.render(painter, self.size(), RenderOutput.SUBJECT)
    finally:
      painter.end()

  def closeEvent(self, e):
    self.task.stop()

class SubjectWindow(QMainWindow):
  def __init__(self, task: Task, view: QWidget):
    super().__init__()
    self.setWindowTitle('Subject')
    self.task = task

    self.setCentralWidget(view)
    self.central_widget = view
    
    self.is_fullscreen = False

    view_menu = self.menuBar().addMenu("&View")
    action = QAction('Enter &Fullscreen', self)
    action.setShortcut('Ctrl+F')
    action.triggered.connect(self.toggle_fullscreen)
    view_menu.addAction(action)

  def toggle_fullscreen(self) -> None: # pylint: disable=invalid-name
    """
    Toggle fullscreen mode
    """
    if self.is_fullscreen:
      self.showNormal()
      self.menuBar().show()
    else:
      self.showFullScreen()
      self.menuBar().hide()
      #self.central_widget.label.setText('Press Esc to exit fullscreen')
      #QTimer.singleShot(1000, lambda: self.central_widget.label.setText(''))
    self.is_fullscreen = not self.is_fullscreen

  def closeEvent(self, e):
    self.task.stop()

  def keyPressEvent(self, event: QKeyEvent) -> None: # pylint: disable=invalid-name
    """
    Toggle fullscreen when the user presses Esc
    """
    if event.key() == Qt.Key.Key_Escape:
      self.toggle_fullscreen()

class Action(typing.NamedTuple):
  do: typing.Callable[[], None]
  undo: typing.Callable[[], None]

class OperatorView(QWidget):
  def __init__(self, task: Task, subject_view: QWidget, config: dict):
    super().__init__()
    self.setWindowTitle('Operator')
    self.config = config
    self.task = task
    self.subject_view = subject_view
    #self.editing_saccades = False
    self.drag_start: QPoint | None = None
    self.base_eye_scaling: dict | None = None
    self.actions = []
    self.actions_position = 0

  def paintEvent(self, e):
    painter = QPainter(self)
    try:
      painter.setBrush(Qt.GlobalColor.gray)
      painter.drawRect(0, 0, 10000, 10000)
      scale = min(self.width()/self.subject_view.width(), self.height()/self.subject_view.height())
      painter.scale(scale, scale)
      self.task.render(painter, self.subject_view.size(), RenderOutput.OPERATOR)
    finally:
      painter.end()

  def keyPressEvent(self, a0: QKeyEvent):
    if a0.key() == Qt.Key.Key_Q:
      self.task.show_fixation = True
    elif a0.key() == Qt.Key.Key_W:
      self.task.show_saccade = True
    #elif a0.key() == Qt.Key.Key_E:
    #  self.editing_saccades = True
    #self.subject_view.update()
    #self.update()

  def do(self, action: Action):
    self.actions = self.actions[:self.actions_position]
    self.actions.append(action)
    self.actions_position += 1
    action.do()

  def undo(self):
    if self.actions_position > 0:
      self.actions_position -= 1
      self.actions[self.actions_position].undo()

  def redo(self):
    if self.actions_position < len(self.actions):
      self.actions[self.actions_position].do()
      self.actions_position += 1

  def keyReleaseEvent(self, a0: QKeyEvent):
    if a0.key() == Qt.Key.Key_Q:
      self.task.show_fixation = False
    elif a0.key() == Qt.Key.Key_W:
      self.task.show_saccade = False
    #elif a0.key() == Qt.Key.Key_E:
    #  self.editing_saccades = False
    elif a0.key() == Qt.Key.Key_R:
      create_task_with_exc_handling(self.task.deliver_reward())
    elif a0.key() == Qt.Key.Key_Left:
      self.task.current_saccade = (self.task.current_saccade - 1) % len(self.task.saccades)
    elif a0.key() == Qt.Key.Key_Right:
      self.task.current_saccade = (self.task.current_saccade + 1) % len(self.task.saccades)
    elif a0.key() == Qt.Key.Key_Z:
      if a0.modifiers() & Qt.KeyboardModifier.ControlModifier:
        if a0.modifiers() & Qt.KeyboardModifier.ShiftModifier:
          self.redo()
        else:
          self.undo()
    elif a0.key() == Qt.Key.Key_Y:
      if a0.modifiers() & Qt.KeyboardModifier.ControlModifier:
        self.redo()

    #self.subject_view.update()
    #self.update()

  def scale_to_subject(self, x, y):
      scale = min(self.width()/self.subject_view.width(), self.height()/self.subject_view.height())
      return int(x/scale - self.subject_view.width()/2), int(y/scale - self.subject_view.height()/2)

  def mousePressEvent(self, event: QMouseEvent):
    if event.button() == Qt.MouseButton.LeftButton and event.modifiers() == Qt.KeyboardModifier.NoModifier: # type: ignore
      self.drag_start = self.scale_to_subject(event.pos().x(), event.pos().y())
      self.task.start_nudge(self.drag_start)

  def mouseReleaseEvent(self, event: QMouseEvent) -> None: # pylint: disable=invalid-name
    print(event, event.button())
    x, y = self.scale_to_subject(event.pos().x(), event.pos().y())
    print(x, y)
    self.drag_start = None
    self.task.stop_nudge()
    editing_saccades = event.modifiers() & Qt.KeyboardModifier.ControlModifier
    if editing_saccades:
      if event.button() == Qt.MouseButton.LeftButton: # type: ignore
        self.do(Action(lambda: self.task.add_target(x, y), lambda: self.task.remove_target(x, y)))
      elif event.button() == Qt.MouseButton.RightButton:
        self.do(Action(lambda: self.task.remove_target(x, y), lambda: self.task.add_target(x, y)))
    else:
      if event.button() == Qt.MouseButton.RightButton:
        saccade_index=self.task.current_saccade
        self.do(Action(
          lambda: self.task.add_training_sample(x, y, saccade_index),
          lambda: self.task.pop_training_sample()))

    #self.subject_view.update()
    #self.update()

  def mouseMoveEvent(self, event: QMouseEvent):
    if self.drag_start is not None:
      drag_current = self.scale_to_subject(event.pos().x(), event.pos().y())
      self.task.nudge(self.drag_start, drag_current)

  def closeEvent(self, e):
    self.task.stop()

class OperatorWindow(QMainWindow):
  def __init__(self, task: Task, view: QWidget, subject_view: QWidget, config: dict):
    super().__init__()
    self.task = task
    view.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

    central_widget = QWidget()

    layout = QVBoxLayout()
    layout.addWidget(view, 1)

    fixation_radius_widget = QSpinBox()
    fixation_radius_widget.setRange(0, 10000)
    saccade_radius_widget = QSpinBox()
    saccade_radius_widget.setRange(0, 10000)
    reward_widget = QSpinBox()
    reward_widget.setRange(0, 10000)
    distance_widget = QDoubleSpinBox()
    distance_widget.setRange(0, 10000)
    distance_widget.setDecimals(3)
    dpi_widget = QDoubleSpinBox()
    dpi_widget.setRange(0, 10000)
    clear_button = QPushButton('Clear')
    fit_button = QPushButton('Fit')
    reset_button = QPushButton('Reset')
    model_label = QLabel('Model:')
    layout.addWidget(model_label)
    layout.addWidget(fit_button)
    layout.addWidget(reset_button)
    layout.addWidget(QLabel('Fixation Radius'))
    layout.addWidget(fixation_radius_widget)
    layout.addWidget(QLabel('Saccade Radius'))
    layout.addWidget(saccade_radius_widget)
    layout.addWidget(QLabel('Reward (ms)'))
    layout.addWidget(reward_widget)
    layout.addWidget(QLabel('Distance (mm)'))
    layout.addWidget(distance_widget)
    layout.addWidget(QLabel('DPI'))
    layout.addWidget(dpi_widget)
    layout.addWidget(clear_button)
    central_widget.setLayout(layout)
    self.setCentralWidget(central_widget)
    central_widget.setFocusProxy(view)

    def on_fixation_radius(val):
      self.task.fixation_radius = val

    def on_saccade_radius(val):
      self.task.saccade_radius = val

    eye_scaling = config['eye_scaling']
    def on_screen_distance(val):
      nonlocal models, projective
      if projective is not None:
        projective['Distance (m)'] = val

    def on_dpi(val):
      nonlocal models, projective
      if projective is not None:
        projective['DPI'] = val

    def on_clear():
      self.task.path.clear()
      self.task.training_path.clear()
      del self.task.points[:]
      del self.task.training_data[:]
      self.view.actions = []

    def on_reward(val):
      self.task.reward_ms = val

    fixation_radius_widget.valueChanged.connect(on_fixation_radius)
    saccade_radius_widget.valueChanged.connect(on_saccade_radius)
    reward_widget.valueChanged.connect(on_reward)
    clear_button.clicked.connect(on_clear)
    fit_button.clicked.connect(self.task.optimize)
    reset_button.clicked.connect(self.task.reset)
    distance_widget.valueChanged.connect(on_screen_distance)
    dpi_widget.valueChanged.connect(on_dpi)
    
    fixation_radius_widget.setValue(50)
    saccade_radius_widget.setValue(100)
    reward_widget.setValue(500)

    models, projective = None, None
    def on_change(source, action, key, value):
      nonlocal models, projective
      #print('OperatorWindow', source is projective, key, value)

      if source is eye_scaling:
        if key == 'Models':
          models = value
          value.recap()
        elif key == 'Selected Model':
          model_label.setText(f'Model: {value}')
      elif source is models:
        if key == 'Projective':
          projective = value
          value.recap()
      elif source is projective:
        if key == 'Distance (m)':
          distance_widget.setValue(value)
        elif key == 'DPI':
          dpi_widget.setValue(value)

    eye_scaling.add_recursive_observer(on_change)
    eye_scaling.recap()

  def closeEvent(self, e):
    self.task.stop()

async def main():
  _ = QApplication(sys.argv)
  
  thread = ThalamusThread('localhost:50050')
  thread_task = await thread.async_start()
  try:
    #thread.config['eye_scaling'].add_recursive_observer(print)
    #thread.config['eye_scaling'].recap()
    task = Task(thread.stub, thread.config)
    await task.prepare_reward()
    task.task = asyncio.create_task(task.run())
    subject = SubjectView(task)
    subject_window = SubjectWindow(task, subject)
    operator = OperatorView(task, subject, thread.config)
    operator_window = OperatorWindow(task, operator, subject, thread.config)

    subject_window.move(0, 0)
    subject_window.resize(800, 800)
    operator_window.move(800, 0)
    operator_window.resize(800, 800)
    subject_window.show()
    operator_window.show()

    while True:
      QApplication.processEvents()
      await asyncio.sleep(.032)
      if task.task.done():
        try:
          await task.task
        except asyncio.CancelledError:
          pass
        break
      subject.update()
      operator.update()
  finally:
    thread_task.cancel()


asyncio.run(main())