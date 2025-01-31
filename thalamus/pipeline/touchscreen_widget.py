from ..qt import *
import asyncio
import typing
import numpy
import grpc
import numpy.linalg
from ..config import ObservableDict
from ..thalamus_pb2 import AnalogRequest, AnalogResponse, NodeRequest, NodeSelector
from ..thalamus_pb2_grpc import ThalamusStub
from ..task_controller.util import create_task_with_exc_handling

class CalibrationWidget(QWidget):
  def __init__(self, config: ObservableDict, stream: typing.AsyncIterable[AnalogResponse], skip_calibration):
    super().__init__()
    self.config = config

    self.screen_points = [QPoint(),
                          QPoint(),
                          QPoint()]
    self.touch_points = [[] for _ in self.screen_points]
    self.current_point = len(self.screen_points) if skip_calibration else 0
    self.path = QPainterPath()
    self.path.setFillRule(Qt.FillRule.WindingFill)
    self.transform = QTransform()

    self.task = create_task_with_exc_handling(self.stream_processor(stream))
    self.px = 0
    self.py = 0

    def on_change(source, action, key, value):
      monitor = config['Monitor']
      screen = QApplication.screens()[monitor]
      geometry = screen.geometry()
      self.screen_points = [QPoint(geometry.x() + geometry.width()//3,   geometry.y() + geometry.height()//3),
                            QPoint(geometry.x() + 2*geometry.width()//3, geometry.y() + geometry.height()//3),
                            QPoint(geometry.x() + geometry.width()//3,   geometry.y() + 2*geometry.height()//3)]
      self.update()

    config.add_recursive_observer(on_change, lambda: isdeleted(self))
    config.recap(lambda *args: on_change(config, *args))

  async def stream_processor(self, stream: typing.AsyncIterable[AnalogResponse]):
    try:
      async for message in stream:
        if len(message.spans) == 0:
          continue

        if len(message.spans) >= 1:
          span = message.spans[0]
          if span.end - span.begin > 0:
            self.px = message.data[span.end-1]

        if len(message.spans) >= 2:
          span = message.spans[1]
          if span.end - span.begin > 0:
            self.py = message.data[span.end-1]
        
        touch = QPointF(self.px, self.py)
        #print(touch, [len(t) for t in self.touch_points])
        if self.current_point < len(self.screen_points):
          if touch.x() < -5 or touch.y() < -5:
            continue
          self.touch_points[self.current_point].append([self.px, self.py, 1])
          self.touch = touch
          self.update()
        elif self.transform is not None:
          screenf = self.transform.map(touch)
          screen = QPoint(int(screenf.x()), int(screenf.y()))
          widget = self.mapFromGlobal(screen)
          self.path.moveTo(widget.x(), widget.y())
          self.path.addEllipse(QPointF(widget), 4, 4)
          self.update()

    except asyncio.CancelledError:
      pass
    except grpc.aio.AioRpcError:
      pass

  def moveEvent(self, e):
    self.update()

  def closeEvent(self, e):
    self.task.cancel()

  def keyReleaseEvent(self, e):
    self.current_point += 1
    if self.current_point == len(self.screen_points):
      screen = numpy.array([[p.x(), p.y(), 1] for p in self.screen_points]).transpose()
      mean_touch_points = numpy.array([numpy.array(p).mean(axis=0) for p in self.touch_points]).transpose()
      transform = numpy.matmul(screen, numpy.linalg.inv(mean_touch_points))
      self.config['Transform'] = [
        [transform[0,0], transform[0,1], transform[0,2]],
        [transform[1,0], transform[1,1], transform[1,2]],
        [transform[2,0], transform[2,1], transform[2,2]]]
    self.update()

  def paintEvent(self, e):
    painter = QPainter(self)
    painter.fillRect(0, 0, self.width(), self.height(), Qt.GlobalColor.black)

    painter.setPen(Qt.GlobalColor.blue)
    painter.fillPath(self.path, Qt.GlobalColor.blue)
   
    if self.current_point < len(self.screen_points):
      screen_point = self.screen_points[self.current_point]
      point = self.mapFromGlobal(screen_point)

      painter.save()
      painter.translate(point.x(), point.y())

      painter.setPen(Qt.GlobalColor.red)
      rotations = [0, 45, 90, 135]
      for rotation in rotations:
        painter.save()
        painter.rotate(rotation)
        painter.drawLine(-1_000_000, 0, 1_000_000, 0)
        painter.restore()

      painter.restore()

      painter.setPen(Qt.GlobalColor.white)
      painter.drawText(QRect(0, 0, self.width(), self.height()), Qt.AlignmentFlag.AlignCenter,
        f'Touch target then press any key to progress.  Got {len(self.touch_points[self.current_point])} samples')
    else:
      painter.setPen(Qt.GlobalColor.white)
      painter.drawText(QRect(0, 0, self.width(), self.height()), Qt.AlignmentFlag.AlignCenter,
        'Touch to test calibration')


class TouchScreenWidget(QWidget):
  def __init__(self, config: ObservableDict, stub: ThalamusStub):
    super().__init__()

    if 'Transform' not in config:
      config['Transform'] = [
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0]
      ]

    if 'Monitor' not in config:
      config['Monitor'] = 0

    layout = QVBoxLayout()
    monitor_spinbox = QSpinBox()
    monitor_spinbox.setMaximum(len(QApplication.screens())-1)
    monitor_layout = QHBoxLayout()
    monitor_layout.addWidget(QLabel('Monitor:'))
    monitor_layout.addWidget(monitor_spinbox)
    calibrate_button = QPushButton('Calibrate')
    test_button = QPushButton('Test')
    layout.addLayout(monitor_layout)
    layout.addWidget(calibrate_button)
    layout.addWidget(test_button)
    layout.addStretch(1)
    self.setLayout(layout)
    calibration_widget: typing.Optional[QWidget] = None

    def on_calibrate(testing):
      nonlocal calibration_widget
      if not testing:
        config['Transform'] = [
          [1.0, 0.0, 0.0],
          [0.0, 1.0, 0.0],
          [0.0, 0.0, 1.0]
        ]
      request = AnalogRequest(
        node = NodeSelector(name=config['name']),
      )
      stream = stub.analog(request)
      if calibration_widget is not None:
        calibration_widget.close()
      calibration_widget = CalibrationWidget(config, stream, testing)
      geometry = qt_screen_geometry()
      calibration_widget.move(QPoint(geometry.width()//4, geometry.height()//4))
      calibration_widget.resize(QSize(geometry.width()//2, geometry.height()//2))
      calibration_widget.show()

    calibrate_button.clicked.connect(lambda: on_calibrate(False))
    test_button.clicked.connect(lambda: on_calibrate(True))

    def on_monitor(v):
      config.update({'Monitor': v})

    monitor_spinbox.valueChanged.connect(on_monitor)

    def on_change(source, action, key, value):
      if key == 'Monitor':
        monitor_spinbox.setValue(value)

    config.add_recursive_observer(on_change, lambda: isdeleted(self))
    config.recap(lambda *args: on_change(config, *args))
