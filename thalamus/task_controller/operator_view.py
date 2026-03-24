"""
Module defining the operator view
"""
import typing
import asyncio
import functools

from ..qt import *

from .window import Window as TaskWindow
from .util import RenderOutput
from .canvas import CanvasOpenGLConfig, create_canvas_opengl_config, create_canvas_painter, update_projection_matrix
from ..config import ObservableCollection

class ViewWidget(QOpenGLWidget):
  """
  Central widget for the operator view
  """
  def __init__(self, target: TaskWindow) -> None:
    super().__init__()
    self.target = target
    self.painting = False
    self.opengl_config: typing.Optional[CanvasOpenGLConfig] = None

  def initializeGL(self) -> None: # pylint: disable=invalid-name
    '''
    Sets up OpenGL resources for the operator preview.
    '''
    self.opengl_config = create_canvas_opengl_config()

  def resizeGL(self, width: int, height: int) -> None: # pylint: disable=invalid-name,unused-argument
    '''
    Projection is updated during paint to match the target canvas dimensions.
    '''

  def paintGL(self) -> None: # pylint: disable=invalid-name
    """
    Renders the target scene directly into this view.
    """
    assert self.opengl_config, 'opengl_config is None'
    try:
      self.painting = True
      update_projection_matrix(self.opengl_config, self.target.canvas.width(), self.target.canvas.height())
      painter = create_canvas_painter(RenderOutput.OPERATOR, self.opengl_config, self)
      render_width = self.target.canvas.width()
      render_height = self.target.canvas.height()
      scale_factor = min(self.width()/render_width, self.height()/render_height)
      offset_x = (self.width() - render_width*scale_factor)/2
      offset_y = (self.height() - render_height*scale_factor)/2

      with painter:
        painter.fillRect(self.rect(), QColor(0, 0, 0))
        painter.translate(offset_x, offset_y)
        painter.scale(scale_factor, scale_factor)
        self.target.canvas.render_frame(painter)
    finally:
      self.painting = False

class CentralWidget(QWidget):
  """
  Central widget for the operator view
  """
  def __init__(self, target: TaskWindow, config: ObservableCollection) -> None:
    super().__init__()

    if 'eye_scaling' not in config:
      config['eye_scaling'] = {}

    eye_config = config['eye_scaling']

    layout = QGridLayout()
    self.view_widget = ViewWidget(target)
    layout.addWidget(self.view_widget, 0, 0, 1, 4)
    layout.setRowStretch(0, 1)

    clear_button = QPushButton('Clear')
    layout.addWidget(clear_button, 1, 0)
    layout.setRowStretch(1, 0)
    clear_button.clicked.connect(target.canvas.clear_accumulation)

    auto_clear_checkbox = QCheckBox('Auto Clear')
    layout.addWidget(auto_clear_checkbox, 1, 1)
    auto_clear_checkbox.toggled.connect(lambda v: eye_config.update({'Auto Clear': v}))

    def on_eye_config_change(a, k, v):
      if k == 'Auto Clear':
        auto_clear_checkbox.setChecked(v)

    eye_config.add_observer(on_eye_config_change, lambda: isdeleted(self))
    eye_config.recap(on_eye_config_change)

    quadrants = [
      ("I", 2, 0),
      ("II", 2, 2),
      ("III", 5, 0),
      ("IV", 5, 2)
    ]

    def update_field(quadrant: str, field: str, value: float) -> None:
      eye_config[quadrant][field] = value

    for quadrant, row, column in quadrants:
      if quadrant not in eye_config:
        eye_config[quadrant] = {'x': 1, 'y': 1}

      layout.addWidget(QLabel(quadrant), row, column)
      layout.setRowStretch(row, 0)
      layout.setRowStretch(row+1, 0)

      x_spin_box = QDoubleSpinBox()
      x_spin_box.setMaximum(1e9)
      x_spin_box.setValue(eye_config[quadrant]['x'])
      x_spin_box.valueChanged.connect(functools.partial(update_field, quadrant, 'x'))
      x_spin_box.setObjectName(f'{quadrant}_x')
      layout.addWidget(x_spin_box, row+1, column)

      y_spin_box = QDoubleSpinBox()
      y_spin_box.setMaximum(1e9)
      y_spin_box.setValue(eye_config[quadrant]['y'])
      y_spin_box.valueChanged.connect(functools.partial(update_field, quadrant, 'y'))
      y_spin_box.setObjectName(f'{quadrant}_y')
      layout.addWidget(y_spin_box, row+1, column+1)

      def on_config_change(quadrant: str, x_box: QDoubleSpinBox, y_box: QDoubleSpinBox,
                           _: ObservableCollection.Action, _key: typing.Any, _value: typing.Any) -> None:
        x_box.setValue(eye_config[quadrant]['x'])
        y_box.setValue(eye_config[quadrant]['y'])

      eye_config[quadrant].add_observer(functools.partial(on_config_change, quadrant, x_spin_box, y_spin_box))

    self.setLayout(layout)

class Window(QMainWindow):
  """
  Root widget for the operator view
  """
  def __init__(self, target: TaskWindow, config: ObservableCollection) -> None:
    super().__init__()
    self.target = target
    self.closed = False
    self.central_widget = CentralWidget(self.target, config)
    self.render_loop = asyncio.get_event_loop().create_task(self.__render_loop())
    self.setCentralWidget(self.central_widget)

  async def __render_loop(self):
    try:
      while True:
        await asyncio.sleep(1/30)
        self.central_widget.view_widget.update()
    except asyncio.CancelledError:
      pass

  def closeEvent(self, event: QCloseEvent) -> None: # pylint: disable=invalid-name
    """
    Remove callback when window closes
    """
    self.closed = True
    self.render_loop.cancel()
    super().closeEvent(event)
