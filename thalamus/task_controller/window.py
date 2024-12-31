"""
Module that defines the window that the tasks are rendered in.
"""

import typing
import os
import asyncio

from ..qt import *

from .canvas import Canvas
from ..config import ObservableCollection
#from .. import recorder2_pb2_grpc
from .. import ophanim_pb2_grpc
from .. import thalamus_pb2_grpc
from .util import TaskContextProtocol

class CentralWidget(QWidget):
  """
  The windows central widget.  Contains the canvas and a QLabel to render over it.
  """
  def __init__(self, content: Canvas) -> None:
    super().__init__()
    self.content = content
    content.setParent(self)

    self.label = QLabel('', self)
    self.label.setObjectName('notification_label')
    self.label.setStyleSheet("QLabel { color : white; font-size: 50px }")
    self.label.setAlignment(Qt.AlignmentFlag.AlignCenter)
    self.label.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)

    size = QSize(self.width(), self.height())
    self.resizeEvent(QResizeEvent(size, size))

  def resizeEvent(self, event: QResizeEvent) -> None: # pylint: disable=invalid-name
    """
    Handles resize events
    """
    self.label.setGeometry(0, 0, event.size().width(), event.size().height())
    self.content.setGeometry(0, 0, event.size().width(), event.size().height())

class Window(QMainWindow):
  """
  The window that the task will render in
  """
  def __init__(self, config: ObservableCollection, done_future: asyncio.Future,
               recorder: typing.Any,#recorder2_pb2_grpc.RecorderStub,
               ophanim: ophanim_pb2_grpc.OphanimStub,
               thalamus: thalamus_pb2_grpc.ThalamusStub,
               port: typing.Optional[int] = None) -> None:
    super().__init__()
    self.done_future = done_future
    self.canvas = Canvas(config, recorder, ophanim, thalamus, port)    
    self.canvas.setWindowIcon(QIcon(os.path.join(os.path.dirname(__file__), 'icon.png')))
    self.canvas.setObjectName('canvas')
    self.central_widget = CentralWidget(self.canvas)
    self.setCentralWidget(self.central_widget)    
    self.setWindowIcon(QIcon(os.path.join(os.path.dirname(__file__), 'icon.png')))

    self.is_fullscreen = False

    view_menu = self.menuBar().addMenu("&View")
    action = QAction('Enter &Fullscreen', self)
    action.setShortcut('Ctrl+F')
    action.triggered.connect(self.toggle_fullscreen)
    view_menu.addAction(action)

    self.paint_subscribers: typing.List[typing.Callable[[], None]] = []

  def set_task_context(self, task_context: TaskContextProtocol):
    self.canvas.set_task_context(task_context)

  def closeEvent(self, event: QCloseEvent) -> None: # pylint: disable=invalid-name
    """
    Stop the ROS loop when the user exists
    """
    self.done_future.set_result(None)
    super().closeEvent(event)

  def get_canvas(self) -> Canvas:
    """
    Returns the canvas
    """
    return self.canvas

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
      self.central_widget.label.setText('Press Esc to exit fullscreen')
      QTimer.singleShot(1000, lambda: self.central_widget.label.setText(''))
    self.is_fullscreen = not self.is_fullscreen

  def paintEvent(self, event: QPaintEvent) -> None: # pylint: disable=invalid-name
    """
    Notify subscribers when this widget paints
    """
    super().paintEvent(event)
    for subscriber in self.paint_subscribers:
      subscriber()

  def keyPressEvent(self, event: QKeyEvent) -> None: # pylint: disable=invalid-name
    """
    Toggle fullscreen when the user presses Esc
    """
    if event.key() == Qt.Key.Key_Escape:
      self.toggle_fullscreen()
