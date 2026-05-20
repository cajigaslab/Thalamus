import re
import sys
import asyncio
import typing
import json

import grpc

from thalamus.qt import *
from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc
from thalamus.task_controller.util import RenderOutput, create_task_with_exc_handling

from thalamus.util import IterableQueue

from thalamus.thread import ThalamusThread

POINT_SIZE = 10
QUAD_TO_LABEL = {
  (True, True): 'IV',
  (True, False): 'II',
  (False, True): 'III',
  (False, False): 'I',
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
    self.paths = {
      (True, True): QPainterPath(),
      (True, False): QPainterPath(),
      (False, True): QPainterPath(),
      (False, False): QPainterPath(),
    }
    self.scales = {
      (True, True): config['eye_scaling'][QUAD_TO_LABEL[(True, True)]],
      (True, False): config['eye_scaling'][QUAD_TO_LABEL[(True, False)]],
      (False, True): config['eye_scaling'][QUAD_TO_LABEL[(False, True)]],
      (False, False): config['eye_scaling'][QUAD_TO_LABEL[(False, False)]],
    }
    self.reward_queue = IterableQueue()

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
    
    fixation_color = QColor(Qt.GlobalColor.white)
    fixation_shape = QRect(size.width()//2 - self.fixation_radius, size.height()//2 - self.fixation_radius,
                           2*self.fixation_radius, 2*self.fixation_radius)
    
    saccade_color = QColor(Qt.GlobalColor.white)
    saccade_shape = QRect(0, 0,
                           2*self.saccade_radius, 2*self.saccade_radius)
    if output == RenderOutput.OPERATOR:
      if not self.show_fixation:
        fixation_color.setAlpha(64)

      painter.setBrush(fixation_color)
      painter.drawEllipse(fixation_shape)

      for i, saccade in enumerate(self.saccades):
        if self.show_saccade and i == self.current_saccade:
          saccade_color.setAlpha(255)
        else:
          saccade_color.setAlpha(64)

        painter.save()
        painter.setBrush(saccade_color)
        if i == self.current_saccade:
          painter.setPen(Qt.GlobalColor.green)
        painter.drawEllipse(saccade_shape.adjusted(saccade.x - self.saccade_radius, saccade.y - self.saccade_radius,
                                                   saccade.x - self.saccade_radius, saccade.y - self.saccade_radius))
        painter.restore()

      for quad, path in self.paths.items():
        painter.save()
        pen = painter.pen()
        pen.setCosmetic(True)
        pen.setWidth(POINT_SIZE)
        pen.setColor(Qt.GlobalColor.blue)
        pen.setCapStyle(Qt.PenCapStyle.RoundCap)
        painter.setPen(pen)
        scale = self.scales[quad]
        painter.translate(size.width()//2, size.height()//2)
        painter.scale(scale['x'], scale['y'])
        painter.setBrush(Qt.GlobalColor.blue)
        painter.drawPath(path)
        painter.restore()

      painter.save()
      painter.setPen(Qt.GlobalColor.yellow)
      painter.drawLine(0, size.height()//2, size.width(), size.height()//2)
      painter.drawLine(size.width()//2, 0, size.width()//2, size.height())
      painter.restore()
      
    elif output == RenderOutput.SUBJECT:
      if self.show_fixation:
        painter.setBrush(fixation_color)
        painter.drawEllipse(fixation_shape)

      if self.saccades and self.show_saccade:
        painter.setBrush(saccade_color)
        saccade = self.saccades[self.current_saccade]
        painter.drawEllipse(saccade_shape.adjusted(saccade.x - self.saccade_radius, saccade.y - self.saccade_radius,
                                                    saccade.x - self.saccade_radius, saccade.y - self.saccade_radius))

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

        point = QPointF(x, -y)
        quadrant = point.x() > 0, point.y() > 0
        path = self.paths[quadrant]
        path.moveTo(point)
        path.lineTo(QPointF(x+1e-6, -y))
        #path.addEllipse(point, POINT_SIZE, POINT_SIZE)

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

class OperatorView(QWidget):
  def __init__(self, task: Task, subject_view: QWidget, config: dict):
    super().__init__()
    self.setWindowTitle('Operator')
    self.config = config
    self.task = task
    self.subject_view = subject_view
    self.editing_saccades = False
    self.drag_start: QPoint | None = None
    self.base_eye_scaling: dict | None = None

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
    elif a0.key() == Qt.Key.Key_E:
      self.editing_saccades = True
    elif a0.key() == Qt.Key.Key_Left:
      self.task.current_saccade = (self.task.current_saccade - 1) % len(self.task.saccades)
    elif a0.key() == Qt.Key.Key_Right:
      self.task.current_saccade = (self.task.current_saccade + 1) % len(self.task.saccades)
    #self.subject_view.update()
    #self.update()

  def keyReleaseEvent(self, a0: QKeyEvent):
    if a0.key() == Qt.Key.Key_Q:
      self.task.show_fixation = False
    elif a0.key() == Qt.Key.Key_W:
      self.task.show_saccade = False
    elif a0.key() == Qt.Key.Key_E:
      self.editing_saccades = False
    elif a0.key() == Qt.Key.Key_R:
      create_task_with_exc_handling(self.task.deliver_reward())
    #self.subject_view.update()
    #self.update()

  def mousePressEvent(self, event: QMouseEvent) -> None: # pylint: disable=invalid-name
    print(event, event.buttons())
    if not self.editing_saccades:
      self.drag_start = event.pos()
      self.base_eye_scaling = self.config['eye_scaling'].copy()

  def update_scaling(self, event: QMouseEvent) -> None:
    if self.drag_start is not None:
      subject_to_operator_scale = min(self.width()/self.subject_view.width(), self.height()/self.subject_view.height())
      center = self.subject_view.width()*subject_to_operator_scale/2, self.subject_view.height()*subject_to_operator_scale/2

      quadrant = self.drag_start.x() > center[0], self.drag_start.y() > center[1]
      flip = 1 if quadrant[0] else -1, 1 if quadrant[1] else -1

      quad_start = flip[0]*(self.drag_start.x() - center[0]), flip[1]*(self.drag_start.y() - center[1])
      quad_end = flip[0]*(event.pos().x() - center[0]), flip[1]*(event.pos().y() - center[1])

      if quad_start[0] <= 0 or quad_start[1] <= 0:
        return

      rescale = quad_end[0]/quad_start[0], quad_end[1]/quad_start[1]
      label = QUAD_TO_LABEL[quadrant]
      print(quadrant, label)
      
      self.config['eye_scaling'][label]['x'] = self.base_eye_scaling[label]['x'] * rescale[0]
      self.config['eye_scaling'][label]['y'] = self.base_eye_scaling[label]['y'] * rescale[1]

  def mouseMoveEvent(self, event: QMouseEvent) -> None:
    self.update_scaling(event)
    
  def mouseReleaseEvent(self, event: QMouseEvent) -> None: # pylint: disable=invalid-name
    print(event, event.button())
    scale = min(self.width()/self.subject_view.width(), self.height()/self.subject_view.height())
    if self.editing_saccades:
      if event.button() == Qt.MouseButton.LeftButton: # type: ignore
        self.task.add_target(int(event.pos().x()/scale), int(event.pos().y()/scale))
      elif event.button() == Qt.MouseButton.RightButton:
        self.task.remove_target(int(event.pos().x()/scale), int(event.pos().y()/scale))
    elif self.drag_start is not None:
      self.update_scaling(event)

    #self.subject_view.update()
    #self.update()

  def closeEvent(self, e):
    self.task.stop()

class OperatorWindow(QMainWindow):
  def __init__(self, task: Task, view: QWidget, subject_view: QWidget):
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
    clear_button = QPushButton('Clear')
    layout.addWidget(QLabel('Fixation Radius'))
    layout.addWidget(fixation_radius_widget)
    layout.addWidget(QLabel('Saccade Radius'))
    layout.addWidget(saccade_radius_widget)
    layout.addWidget(QLabel('Reward (ms)'))
    layout.addWidget(reward_widget)
    layout.addWidget(clear_button)
    central_widget.setLayout(layout)
    self.setCentralWidget(central_widget)

    def on_fixation_radius(val):
      self.task.fixation_radius = val

    def on_saccade_radius(val):
      self.task.saccade_radius = val

    def on_clear():
      for path in self.task.paths.values():
        path.clear()

    def on_reward(val):
      self.task.reward_ms = val

    fixation_radius_widget.valueChanged.connect(on_fixation_radius)
    saccade_radius_widget.valueChanged.connect(on_saccade_radius)
    reward_widget.valueChanged.connect(on_reward)
    clear_button.clicked.connect(on_clear)
    
    fixation_radius_widget.setValue(50)
    saccade_radius_widget.setValue(100)
    reward_widget.setValue(500)

  def closeEvent(self, e):
    self.task.stop()

async def main():
  _ = QApplication(sys.argv)
  
  thread = ThalamusThread('localhost:50050')
  thread_task = await thread.async_start()
  try:
    thread.config['eye_scaling'].add_recursive_observer(print)
    thread.config['eye_scaling'].recap()
    task = Task(thread.stub, thread.config)
    await task.prepare_reward()
    task.task = asyncio.create_task(task.run())
    subject = SubjectView(task)
    subject_window = SubjectWindow(task, subject)
    operator = OperatorView(task, subject, thread.config)
    operator_window = OperatorWindow(task, operator, subject)

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