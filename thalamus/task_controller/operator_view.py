"""
Module defining the operator view
"""
import typing
import asyncio
import functools

from ..qt import *

import packaging.version
import numpy

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

class AngularScalingModelWidget(QWidget):
  def __init__(self, eye_config: ObservableCollection) -> None:
    super().__init__()
    self.setMinimumSize(100, 100)

    if 'Models' not in eye_config:
      eye_config['Models'] = {}
    
    if 'Projective' not in eye_config['Models']:
      eye_config['Models']['Projective'] = {
        'Angle': [],
        'Scale X': [],
        'Scale Y': [],
      }

    self.model = eye_config['Models']['Angular Scaling']

    def on_change(source: ObservableCollection, _: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      self.update()

    self.model.add_recursive_observer(on_change, lambda: isdeleted(self))
    self.model.recap()

  def paintEvent(self, e):
    painter = QPainter(self)

    anglef = self.model['Angle']
    scalexf = self.model['Scale X']
    scaleyf = self.model['Scale Y']
    length = min(len(anglef), len(scalexf), len(scaleyf))

    diameter = min(self.width(), self.height()) - 10
    radius = diameter/2

    angles = numpy.linspace(0, 2*numpy.pi, 360)
    if length:
      scalesx = numpy.interp(angles, anglef[:length], scalexf[:length], period=2*numpy.pi)
      scalesy = numpy.interp(angles, anglef[:length], scaleyf[:length], period=2*numpy.pi)
      mag = (scalesx**2 + scalesy**2).max()**.5
      scalesx /= mag
      scalesy /= mag
    else:
      scalesx = numpy.ones_like(angles)
      scalesy = numpy.ones_like(angles)

    end = None
    first = True
    path = QPainterPath()
    for a, sx, sy in zip(angles, scalesx, scalesy):
      coord = radius*numpy.cos(a)*sx, radius*numpy.sin(a)*sy
      if first:
        end = coord
        path.moveTo(*coord)
        first = False
      else:
        path.lineTo(*coord)
    if end is not None:
      path.lineTo(*end)
    path.moveTo(0, -radius)
    path.lineTo(0, radius)
    path.moveTo(-radius, 0)
    path.lineTo(radius, 0)

    painter.translate(radius, radius)
    painter.drawPath(path)

class EyeProjectiveModelWidget(QWidget):
  def __init__(self, eye_config: ObservableCollection) -> None:
    super().__init__()

    if 'Models' not in eye_config:
      eye_config['Models'] = {}
    
    if 'Projective' not in eye_config['Models']:
      eye_config['Models']['Projective'] = {
        'Parameters': [0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0],
        'DPI': 100.0,
        'Distance (m)': 1.0
      }

    projective = eye_config['Models']['Projective']
    parameters = projective['Parameters']
    parameter_edit = QLineEdit()
    dpi_spinbox = QDoubleSpinBox()
    dpi_spinbox.setRange(0, 1000000)
    distance_spinbox = QDoubleSpinBox()
    distance_spinbox.setRange(0, 1000000)
    distance_spinbox.setDecimals(3)

    def on_change(source: ObservableCollection, _: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
      nonlocal parameters
      if key == 'Parameters':
        parameters = value
        parameters.recap()
      elif source == parameters:
        parameter_edit.setText(' '.join(str(p) for p in parameters))
      elif key == 'DPI':
        dpi_spinbox.setValue(value)
      elif key == 'Distance (m)':
        distance_spinbox.setValue(value)

    def on_edit():
      nonlocal parameters
      text = parameter_edit.text()
      try:
        new_params = [float(p) for p in text.split()]
        if len(new_params) != 8:
          raise ValueError()
      except ValueError:
        parameter_edit.setStyleSheet("color: red;")
        return
      parameter_edit.setStyleSheet("color: black;")
      parameters[:] = new_params

    parameter_edit.editingFinished.connect(on_edit)
    dpi_spinbox.editingFinished.connect(lambda: projective.update({'DPI': dpi_spinbox.value()}))
    distance_spinbox.editingFinished.connect(lambda: projective.update({'Distance (m)': distance_spinbox.value()}))

    layout = QGridLayout()
    layout.addWidget(QLabel('Parameters:'), 0, 0)
    layout.addWidget(parameter_edit, 0, 1)
    layout.addWidget(QLabel('DPI:'), 1, 0)
    layout.addWidget(dpi_spinbox, 1, 1)
    layout.addWidget(QLabel('Distance (m)'), 2, 0)
    layout.addWidget(distance_spinbox, 2, 1)

    self.setLayout(layout)
    projective.add_recursive_observer(on_change, lambda: isdeleted(self))
    projective.recap()

class EyeQuadrantScalingWidget(QWidget):
  def __init__(self, eye_config: ObservableCollection) -> None:
    super().__init__()

    quadrants = [
      ("I", 2, 0),
      ("II", 2, 2),
      ("III", 5, 0),
      ("IV", 5, 2)
    ]

    layout = QGridLayout()

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

      eye_config[quadrant].add_observer(functools.partial(on_config_change, quadrant, x_spin_box, y_spin_box), lambda: isdeleted(self))

    self.setLayout(layout)


class CentralWidget(QWidget):
  """
  Central widget for the operator view
  """
  def __init__(self, target: TaskWindow, config: ObservableCollection) -> None:
    super().__init__()

    if 'eye_scaling' not in config:
      config['eye_scaling'] = {}

    eye_config = config['eye_scaling']
    if 'Selected Model' not in eye_config:
      eye_config['Selected Model'] = 'Quadrant Scaling'

    layout = QGridLayout()
    layout.addWidget(ViewWidget(target), 0, 0, 1, 4)
    layout.setRowStretch(0, 1)

    clear_button = QPushButton('Clear')
    layout.addWidget(clear_button, 1, 0)
    layout.setRowStretch(1, 0)
    clear_button.clicked.connect(target.canvas.clear_accumulation)

    auto_clear_checkbox = QCheckBox('Auto Clear')
    layout.addWidget(auto_clear_checkbox, 1, 1)
    auto_clear_checkbox.toggled.connect(lambda v: eye_config.update({'Auto Clear': v}))

    model_combo = QComboBox()
    model_combo.addItem('Quadrant Scaling')
    model_combo.addItem('Angular Scaling')
    model_combo.addItem('Projective')
    layout.addWidget(QLabel('Model:'), 2, 0)
    layout.addWidget(model_combo, 2, 1)

    model_combo.currentTextChanged.connect(lambda t: eye_config.update({'Selected Model': t}))

    model_widget = None
    def on_eye_config_change(a, k, v):
      nonlocal model_widget
      if k == 'Auto Clear':
        auto_clear_checkbox.setChecked(v)
      elif k == 'Selected Model':
        model_combo.setCurrentText(v)
        if v == 'Quadrant Scaling':
          new_model_widget = EyeQuadrantScalingWidget(eye_config)
        elif v == 'Projective':
          new_model_widget = EyeProjectiveModelWidget(eye_config)
        elif v == 'Angular Scaling':
          new_model_widget = AngularScalingModelWidget(eye_config)
        else:
          new_model_widget = QWidget()

        
        if model_widget is None:
          model_widget = new_model_widget
          layout.addWidget(model_widget, 3, 0, 1, 4)
        else:
          layout.replaceWidget(model_widget, new_model_widget)
          model_widget.deleteLater()
          model_widget = new_model_widget

    eye_config.add_observer(on_eye_config_change, lambda: isdeleted(self))
    eye_config.recap(on_eye_config_change)

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
