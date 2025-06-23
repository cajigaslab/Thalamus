"""
Module defining the operator view
"""
import typing
import asyncio
import functools

from ..qt import *

import packaging.version

PYQT_VERSION = packaging.version.parse(PYQT_VERSION_STR)
USINGLEGACY_QT = PYQT_VERSION < packaging.version.parse('5.11.0')

from .window import Window as TaskWindow
from .util import RenderOutput
from ..config import ObservableCollection

class ViewWidget(QWidget):
  """
  Central widget for the operator view
  """
  def __init__(self, target: TaskWindow) -> None:
    super().__init__()
    self.target = target
    self.painting = False

  def paintEvent(self, _: QPaintEvent) -> None: # pylint: disable=invalid-name
    """
    Renders the target widget into this view
    """
    try:
      self.painting = True
      if USINGLEGACY_QT:
        with self.target.canvas.masked(RenderOutput.OPERATOR):
          image = self.target.canvas.grabFramebuffer()
      else:
        image = QImage(self.target.width(), self.target.height(),
                                   QImage.Format.Format_RGB32) # type: ignore # pylint: disable=no-member
        with self.target.canvas.masked(RenderOutput.OPERATOR):
          self.target.canvas.render(image)


      painter = QPainter(self)

      scale_factor = min(self.width()/self.target.width(), self.height()/self.target.height())
      render_width = int(self.target.width()*scale_factor)
      render_height = int(self.target.height()*scale_factor)
      render_x = int((self.width() - render_width)/2)
      render_y = int((self.height() - render_height)/2)
      render_rect = QRect(render_x, render_y, render_width, render_height)
      painter.drawImage(render_rect, image)
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
    layout.addWidget(ViewWidget(target), 0, 0, 1, 4)
    layout.setRowStretch(0, 1)

    clear_button = QPushButton('Clear Gaze History') # this button was added to the Operator View to clear the gaze history
    layout.addWidget(clear_button, 1, 0)
    layout.setRowStretch(1, 0)
    clear_button.clicked.connect(target.canvas.clear_accumulation)

    # Adding extra button to clear the canvas
    def on_test():
      target.canvas.do_clear = True
    clear_endpoints_button = QPushButton('Clear Endpoints') # this button was added to the Operator View to clear the endpoints
    layout.addWidget(clear_endpoints_button, 1, 2)
    layout.setRowStretch(1, 0)
    clear_endpoints_button.clicked.connect(on_test)
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
        self.central_widget.update()
    except asyncio.CancelledError:
      pass

  def closeEvent(self, event: QCloseEvent) -> None: # pylint: disable=invalid-name
    """
    Remove callback when window closes
    """
    self.closed = True
    self.render_loop.cancel()
    super().closeEvent(event)
