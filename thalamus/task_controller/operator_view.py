"""
Module defining the operator view
"""
import typing
import functools
import datetime

from ..qt import *

import packaging.version

PYQT_VERSION = packaging.version.parse(PYQT_VERSION_STR)
USINGLEGACY_QT = PYQT_VERSION < packaging.version.parse('5.11.0')

from .window import Window as TaskWindow
from .util import RenderOutput
from ..config import ObservableCollection
from ..util import MeteredUpdater

QUADRANT_LABELS = {
  'I': 'Top Right',
  'II': 'Top Left',
  'III': 'Bottom Left',
  'IV': 'Bottom Right',
}

QUADRANT_SIGNS = {
  'I': (1, -1),
  'II': (-1, -1),
  'III': (-1, 1),
  'IV': (1, 1),
}

class EyeScalingPreview(QWidget):
  """
  Small preview showing how horizontal and vertical gain affect a quadrant.
  """
  def __init__(self, quadrant: str, eye_config: ObservableCollection) -> None:
    super().__init__()
    self.quadrant = quadrant
    self.eye_config = eye_config
    self.setMinimumSize(72, 72)

  def set_quadrant(self, quadrant: str) -> None:
    self.quadrant = quadrant
    self.update()

  def paintEvent(self, _: QPaintEvent) -> None: # pylint: disable=invalid-name
    painter = QPainter(self)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    painter.fillRect(self.rect(), QColor(18, 18, 18))

    outer = self.rect().adjusted(6, 6, -6, -6)
    center = QPointF(outer.center())

    painter.setPen(QPen(QColor(80, 80, 80), 1))
    painter.drawRect(outer)
    painter.drawLine(int(outer.left()), int(center.y()), int(outer.right()), int(center.y()))
    painter.drawLine(int(center.x()), int(outer.top()), int(center.x()), int(outer.bottom()))

    x_gain = float(self.eye_config[self.quadrant]['x'])
    y_gain = float(self.eye_config[self.quadrant]['y'])
    x_sign, y_sign = QUADRANT_SIGNS[self.quadrant]

    max_offset_x = max(8.0, outer.width() * 0.32)
    max_offset_y = max(8.0, outer.height() * 0.32)
    offset_x = max(4.0, min(max_offset_x, max_offset_x * min(x_gain, 2.0) / 2.0))
    offset_y = max(4.0, min(max_offset_y, max_offset_y * min(y_gain, 2.0) / 2.0))

    target = QPointF(center.x() + x_sign * offset_x, center.y() + y_sign * offset_y)

    painter.setPen(QPen(QColor(90, 170, 255), 2))
    painter.drawLine(center, QPointF(target.x(), center.y()))
    painter.drawLine(center, QPointF(center.x(), target.y()))

    painter.setPen(Qt.PenStyle.NoPen)
    painter.setBrush(QColor(90, 220, 255))
    painter.drawEllipse(target, 4.5, 4.5)

    painter.setPen(QPen(QColor(220, 220, 220), 1))
    painter.drawText(outer.adjusted(4, 2, -4, -2), Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeft, 'H')
    painter.drawText(outer.adjusted(4, 2, -4, -2), Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight, 'V')

class ViewWidget(QWidget):
  """
  Central widget for the operator view
  """
  def __init__(self, target: TaskWindow, config: ObservableCollection) -> None:
    super().__init__()
    self.target = target
    self.config = config
    self.painting = False
    self.capture_pending = False
    self.latest_image: typing.Optional[QImage] = None

  def request_capture(self) -> None:
    """
    Schedule a capture after the current paint cycle completes.
    """
    if self.capture_pending or not self.isVisible():
      return
    self.capture_pending = True
    QTimer.singleShot(0, self.capture_frame)

  def capture_frame(self) -> None:
    """
    Capture the operator view into an off-screen image.

    `framebuffer` is visually faithful for QOpenGLWidget content but can lag
    because it forces a GPU readback. `render` is the original lighter-weight
    path and can be switched back on while behavior is running.
    """
    self.capture_pending = False
    if self.painting or not self.isVisible():
      return

    canvas_size = self.target.canvas.size()
    if canvas_size.isEmpty():
      return

    operator_config = self.config['operator_view'] if 'operator_view' in self.config else {}
    capture_mode = operator_config.get('capture_mode', 'framebuffer')

    if capture_mode == 'render':
      device_pixel_ratio = self.target.canvas.devicePixelRatioF()
      image = QImage(int(canvas_size.width() * device_pixel_ratio),
                     int(canvas_size.height() * device_pixel_ratio),
                     QImage.Format.Format_ARGB32_Premultiplied) # type: ignore # pylint: disable=no-member
      image.fill(QColor(0, 0, 0, 255))
      image.setDevicePixelRatio(device_pixel_ratio)
      with self.target.canvas.masked(RenderOutput.OPERATOR):
        self.target.canvas.render(image)
    else:
      with self.target.canvas.masked(RenderOutput.OPERATOR):
        image = self.target.canvas.grabFramebuffer()

    self.latest_image = image
    self.update()

  def paintEvent(self, _: QPaintEvent) -> None: # pylint: disable=invalid-name
    """
    Draw the last captured operator frame.
    """
    try:
      self.painting = True
      canvas_size = self.target.canvas.size()
      painter = QPainter(self)
      painter.fillRect(self.rect(), QColor(0, 0, 0))

      if self.latest_image is None or canvas_size.isEmpty():
        return

      scale_factor = min(self.width()/canvas_size.width(), self.height()/canvas_size.height())
      render_width = int(canvas_size.width()*scale_factor)
      render_height = int(canvas_size.height()*scale_factor)
      render_x = int((self.width() - render_width)/2)
      render_y = int((self.height() - render_height)/2)
      render_rect = QRect(render_x, render_y, render_width, render_height)
      painter.drawImage(render_rect, self.latest_image)
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
    if 'operator_view' not in config:
      config['operator_view'] = {}

    eye_config = config['eye_scaling']
    operator_config = config['operator_view']
    if 'show_touch' not in operator_config:
      operator_config['show_touch'] = True
    if 'show_gaze' not in operator_config:
      operator_config['show_gaze'] = True
    if 'capture_mode' not in operator_config:
      operator_config['capture_mode'] = 'framebuffer'
    if 'show_eye_scaling' not in operator_config:
      operator_config['show_eye_scaling'] = False
    if 'auto_launch' not in operator_config:
      operator_config['auto_launch'] = False

    layout = QGridLayout()
    self.view_widget = ViewWidget(target, config)
    layout.addWidget(self.view_widget, 0, 0, 1, 4)
    layout.setRowStretch(0, 1)

    clear_button = QPushButton('Clear')
    layout.addWidget(clear_button, 1, 0)
    layout.setRowStretch(1, 0)
    clear_button.clicked.connect(target.canvas.clear_accumulation)

    auto_clear_checkbox = QCheckBox('Auto Clear')
    layout.addWidget(auto_clear_checkbox, 1, 1)
    auto_clear_checkbox.toggled.connect(lambda v: eye_config.update({'Auto Clear': v}))

    show_touch_checkbox = QCheckBox('Show Touch')
    show_touch_checkbox.setChecked(operator_config['show_touch'])
    layout.addWidget(show_touch_checkbox, 1, 2)
    show_touch_checkbox.toggled.connect(lambda v: operator_config.update({'show_touch': v}))

    show_gaze_checkbox = QCheckBox('Show Gaze')
    show_gaze_checkbox.setChecked(operator_config['show_gaze'])
    layout.addWidget(show_gaze_checkbox, 1, 3)
    show_gaze_checkbox.toggled.connect(lambda v: operator_config.update({'show_gaze': v}))

    auto_launch_checkbox = QCheckBox('Launch On Startup')
    auto_launch_checkbox.setChecked(operator_config['auto_launch'])
    layout.addWidget(auto_launch_checkbox, 2, 0, 1, 2)
    auto_launch_checkbox.toggled.connect(lambda v: operator_config.update({'auto_launch': v}))

    toggle_eye_scaling_button = QPushButton(
      'Hide Eye Scaling' if operator_config['show_eye_scaling'] else 'Show Eye Scaling')
    layout.addWidget(toggle_eye_scaling_button, 2, 2, 1, 2)
    toggle_eye_scaling_button.clicked.connect(
      lambda: operator_config.update({'show_eye_scaling': not bool(operator_config['show_eye_scaling'])}))

    capture_mode_combo = QComboBox()
    capture_mode_combo.addItem('Framebuffer', 'framebuffer')
    capture_mode_combo.addItem('Render', 'render')
    capture_mode_index = capture_mode_combo.findData(operator_config['capture_mode'])
    if capture_mode_index >= 0:
      capture_mode_combo.setCurrentIndex(capture_mode_index)
    layout.addWidget(QLabel('Capture'), 10, 0)
    layout.addWidget(capture_mode_combo, 10, 1, 1, 3)
    capture_mode_combo.currentIndexChanged.connect(
      lambda _: operator_config.update({'capture_mode': capture_mode_combo.currentData()}))

    def on_eye_config_change(a, k, v):
      if k == 'Auto Clear':
        auto_clear_checkbox.setChecked(v)

    eye_config.add_observer(on_eye_config_change, lambda: isdeleted(self))
    eye_config.recap(on_eye_config_change)

    def on_operator_config_change(_a, key, value):
      if key == 'show_touch':
        show_touch_checkbox.setChecked(bool(value))
      elif key == 'show_gaze':
        show_gaze_checkbox.setChecked(bool(value))
      elif key == 'auto_launch':
        auto_launch_checkbox.setChecked(bool(value))
      elif key == 'show_eye_scaling':
        is_visible = bool(value)
        toggle_eye_scaling_button.setText('Hide Eye Scaling' if is_visible else 'Show Eye Scaling')
        for widget in eye_scaling_widgets:
          widget.setVisible(is_visible)
      elif key == 'capture_mode':
        index = capture_mode_combo.findData(value)
        if index >= 0 and capture_mode_combo.currentIndex() != index:
          capture_mode_combo.setCurrentIndex(index)
        self.view_widget.request_capture()

    eye_scaling_widgets: typing.Tuple[QWidget, ...] = ()

    def update_field(quadrant: str, field: str, value: float) -> None:
      eye_config[quadrant][field] = value

    for quadrant in QUADRANT_LABELS:
      if quadrant not in eye_config:
        eye_config[quadrant] = {'x': 1, 'y': 1}

    selected_quadrant = 'I'

    eye_scaling_header = QLabel('Eye Scaling')
    layout.addWidget(eye_scaling_header, 3, 0, 1, 4)

    quadrant_combo = QComboBox()
    for quadrant, label in QUADRANT_LABELS.items():
      quadrant_combo.addItem(label, quadrant)
    region_label = QLabel('Region')
    layout.addWidget(region_label, 4, 0)
    layout.addWidget(quadrant_combo, 4, 1, 1, 3)

    preview = EyeScalingPreview(selected_quadrant, eye_config)
    preview.setMinimumSize(140, 140)
    layout.addWidget(preview, 5, 0, 1, 4)

    x_spin_box = QDoubleSpinBox()
    x_spin_box.setMaximum(1e9)
    x_spin_box.setObjectName('selected_quadrant_x')
    horizontal_label = QLabel('Horizontal')
    layout.addWidget(horizontal_label, 6, 0, 1, 2)
    layout.addWidget(x_spin_box, 7, 0, 1, 2)

    y_spin_box = QDoubleSpinBox()
    y_spin_box.setMaximum(1e9)
    y_spin_box.setObjectName('selected_quadrant_y')
    vertical_label = QLabel('Vertical')
    layout.addWidget(vertical_label, 6, 2, 1, 2)
    layout.addWidget(y_spin_box, 7, 2, 1, 2)

    selection_label = QLabel(QUADRANT_LABELS[selected_quadrant])
    selection_label.setStyleSheet('font-weight: 600;')
    layout.addWidget(selection_label, 8, 0, 1, 4)

    def refresh_selected_controls() -> None:
      quadrant = quadrant_combo.currentData()
      if quadrant is None:
        return
      selection_label.setText(QUADRANT_LABELS[quadrant])
      preview.set_quadrant(quadrant)
      x_spin_box.blockSignals(True)
      y_spin_box.blockSignals(True)
      x_spin_box.setValue(eye_config[quadrant]['x'])
      y_spin_box.setValue(eye_config[quadrant]['y'])
      x_spin_box.blockSignals(False)
      y_spin_box.blockSignals(False)

    quadrant_combo.currentIndexChanged.connect(lambda _: refresh_selected_controls())
    x_spin_box.valueChanged.connect(
      lambda value: update_field(quadrant_combo.currentData(), 'x', value))
    y_spin_box.valueChanged.connect(
      lambda value: update_field(quadrant_combo.currentData(), 'y', value))

    def on_quadrant_config_change(_: ObservableCollection.Action, _key: typing.Any, _value: typing.Any) -> None:
      refresh_selected_controls()
      preview.update()

    for quadrant in QUADRANT_LABELS:
      eye_config[quadrant].add_observer(on_quadrant_config_change)

    refresh_selected_controls()

    eye_scaling_widgets = (
      eye_scaling_header,
      region_label,
      quadrant_combo,
      preview,
      horizontal_label,
      x_spin_box,
      vertical_label,
      y_spin_box,
      selection_label,
    )
    for widget in eye_scaling_widgets:
      widget.setVisible(bool(operator_config['show_eye_scaling']))

    operator_config.add_observer(on_operator_config_change, lambda: isdeleted(self))
    operator_config.recap(on_operator_config_change)

    self.setLayout(layout)

class Window(QMainWindow):
  """
  Root widget for the operator view
  """
  def __init__(self, target: TaskWindow, config: ObservableCollection) -> None:
    super().__init__()
    self.target = target
    self.config = config
    self.closed = False
    if 'operator_view' not in self.config:
      self.config['operator_view'] = {}
    operator_config = self.config['operator_view']
    if 'view_geometry' not in operator_config:
      operator_config['view_geometry'] = [100, 100, 900, 700]
    self.view_geometry_updater = MeteredUpdater(
      operator_config['view_geometry'],
      datetime.timedelta(seconds=1),
      lambda: isdeleted(self))
    self.central_widget = CentralWidget(self.target, config)
    self.target.canvas.listeners.paint_subscribers.append(self.central_widget.view_widget.request_capture)
    self.setCentralWidget(self.central_widget)
    self.setWindowTitle('Operator View')
    x, y, w, h = operator_config['view_geometry']
    self.move(x, y)
    self.resize(w, h)
    self.central_widget.view_widget.request_capture()

  def moveEvent(self, event: QMoveEvent) -> None: # pylint: disable=invalid-name
    offset = self.frameGeometry().size() - self.geometry().size()
    position = event.pos() - QPoint(offset.width(), offset.height())
    position = QPoint(max(0, position.x()), max(0, position.y()))
    self.view_geometry_updater[:2] = position.x(), position.y()
    super().moveEvent(event)

  def resizeEvent(self, event: QResizeEvent) -> None: # pylint: disable=invalid-name
    self.view_geometry_updater[2:] = event.size().width(), event.size().height()
    super().resizeEvent(event)

  def closeEvent(self, event: QCloseEvent) -> None: # pylint: disable=invalid-name
    """
    Remove callback when window closes
    """
    self.closed = True
    try:
      self.target.canvas.listeners.paint_subscribers.remove(self.central_widget.view_widget.request_capture)
    except ValueError:
      pass
    super().closeEvent(event)
