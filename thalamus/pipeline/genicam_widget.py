import enum
import json
from ..qt import *
from ..task_controller.util import create_task_with_exc_handling
from .. import thalamus_pb2

class Edge(enum.Enum):
  LEFT = enum.auto()
  RIGHT = enum.auto()
  TOP = enum.auto()
  BOTTOM = enum.auto()
  NONE = enum.auto()

class RoiWidget(QWidget):
  def __init__(self, config):
    super().__init__()
    self.config = config
    self.observer = None
    self.next_config = None
    self.edge = Edge.NONE
    self.dragging = False
    self.setMouseTracking(True)
    policy = self.sizePolicy()

    new_policy = QSizePolicy(QSizePolicy.Policy.Expanding,
                             policy.verticalPolicy(),
                             policy.controlType())
    new_policy.setHeightForWidth(True)
    self.setSizePolicy(new_policy)

    def on_change(a, k, v):
      self.updateGeometry()
      self.update()

    self.config.add_observer(on_change, lambda: isdeleted(self))

  def isReady(self):
    return ("OffsetX" in self.config 
            and "OffsetY" in self.config 
            and "Width" in self.config 
            and "Height" in self.config 
            and "WidthMax" in self.config 
            and "HeightMax" in self.config)

  def hasHeightForWidth(self):
    return True

  def heightForWidth(self, width):
    if self.isReady():
      return int(width/self.config['WidthMax']*self.config['HeightMax'])
    else:
      return -1

  def mousePressEvent(self, e: QMouseEvent) -> None:
    if not self.isReady():
      return

    self.dragging = True
    self.next_config = self.config.unwrap()

  def mouseReleaseEvent(self, _: QMouseEvent) -> None:
    if not self.isReady():
      return

    assert self.next_config is not None

    if self.edge == Edge.LEFT:
      if int(self.next_config['OffsetX']) < self.config['OffsetX']:
        self.config['OffsetX'] = int(self.next_config['OffsetX'])
        self.config['Width'] = int(self.next_config['Width'])
      else:
        self.config['Width'] = int(self.next_config['Width'])
        self.config['OffsetX'] = int(self.next_config['OffsetX'])
    elif self.edge == Edge.TOP:
      if int(self.next_config['OffsetY']) < self.config['OffsetY']:
        self.config['OffsetY'] = int(self.next_config['OffsetY'])
        self.config['Height'] = int(self.next_config['Height'])
      else:
        self.config['Height'] = int(self.next_config['Height'])
        self.config['OffsetY'] = int(self.next_config['OffsetY'])
    elif self.edge == Edge.RIGHT:
      self.config['Width'] = int(self.next_config['Width'])
    elif self.edge == Edge.BOTTOM:
      self.config['Height'] = int(self.next_config['Height'])

    self.next_config = None
    self.dragging = False

    self.update()

  def mouseMoveEvent(self, e: QMouseEvent) -> None:
    if not self.isReady():
      return


    scale = min(self.width()/self.config['WidthMax'], self.height()/self.config['HeightMax'])
    x, y = max(0, min(scale*self.config['WidthMax'], e.pos().x())), max(0, min(scale*self.config['HeightMax'], e.pos().y()))
    x, y = int(x/scale), int(y/scale)

    if self.dragging:
      assert self.next_config is not None
      if self.edge == Edge.LEFT:
        new_value = x
        change = self.next_config['OffsetX'] - new_value
        self.next_config['OffsetX'] = new_value
        self.next_config['Width'] += change
      elif self.edge == Edge.TOP:
        new_value = y
        change = self.next_config['OffsetY'] - new_value
        self.next_config['OffsetY'] = new_value
        self.next_config['Height'] += change
      elif self.edge == Edge.RIGHT:
        self.next_config['Width'] = x - self.next_config['OffsetX']
      elif self.edge == Edge.BOTTOM:
        self.next_config['Height'] = y - self.next_config['OffsetY']
    else:
      if abs(self.config['OffsetX']*scale - e.pos().x()) < 10:
        self.setCursor(Qt.CursorShape.SplitHCursor)
        self.edge = Edge.LEFT
      elif abs(self.config['OffsetY']*scale - e.pos().y()) < 10:
        self.setCursor(Qt.CursorShape.SplitVCursor)
        self.edge = Edge.TOP
      elif abs((self.config['OffsetX'] + self.config['Width'])*scale - e.pos().x()) < 10:
        self.setCursor(Qt.CursorShape.SplitHCursor)
        self.edge = Edge.RIGHT
      elif abs((self.config['OffsetY'] + self.config['Height'])*scale - e.pos().y()) < 10:
        self.setCursor(Qt.CursorShape.SplitVCursor)
        self.edge = Edge.BOTTOM
      else:
        self.unsetCursor()
        self.edge = Edge.NONE

    self.update()

  def paintEvent(self, e):
    if not self.isReady():
      return

    painter = QPainter(self)

    roi = self.next_config if self.next_config else self.config
    
    scale = min(self.width()/roi['WidthMax'], self.height()/roi['HeightMax'])
    painter.fillRect(0, 0, self.width(), self.height(), QColor(0, 0, 255))
    painter.save()
    painter.scale(scale, scale)
    painter.fillRect(0, 0, roi['WidthMax'], roi['HeightMax'], QColor(255, 0, 0))
    painter.fillRect(roi['OffsetX'], roi['OffsetY'], roi['Width'], roi['Height'],
                     QColor(0, 255, 0))
    painter.drawRect(roi['OffsetX'], roi['OffsetY'], roi['Width'], roi['Height'])
    painter.restore()

    if not self.isEnabled():
      painter.fillRect(0, 0, self.width(), self.height(), QColor(128, 128, 128, 128))

class GenicamComboBox(QComboBox):
  def __init__(self, config, stub):
    super().__init__()
    self.stub = stub
    self.config = config
    self.loaded = False

  async def asyncShowPopup(self):
    print('asyncShowPopup')
    if self.loaded:
      super().showPopup()
      return

    name = self.config['name']
    current_camera = self.config.get('Camera', None)

    response = await self.stub.node_request(thalamus_pb2.NodeRequest(node=name,json="\"get_cameras\""))
    cameras = json.loads(response.json)
    print('asyncShowPopup', cameras)
    self.clear()
    if cameras is None:
      return
    self.addItems(cameras)
    for i, v in enumerate(cameras):
      if v == current_camera:
        self.setCurrentIndex(i)
        break
    self.loaded = True
    super().showPopup()

  def setCurrentText(self, text):
    if self.loaded:
      return super().setCurrentText(text);
    for i in range(self.count()):
      if self.itemText(i) == text:
        super().setCurrentText(text)
        return

    self.addItem(text)
    super().setCurrentText(text)


  def showPopup(self):
    print('showPopup')
    create_task_with_exc_handling(self.asyncShowPopup())

class GenicamWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    self.config = config
    self.stub = stub

    if 'Running' not in config:
      config['Running'] = False

    config.add_observer(self.on_change, lambda: isdeleted(self))

    layout = QVBoxLayout()

    self.camera_combobox = GenicamComboBox(config, stub)
    self.camera_combobox.currentTextChanged.connect(lambda new_camera: config.update({"Camera": new_camera}))
    layout.addWidget(self.camera_combobox)

    self.running_checkbox = QCheckBox('Running')
    self.running_checkbox.toggled.connect(lambda value: config.update({'Running': value}))
    layout.addWidget(self.running_checkbox)

    self.roi_widget = RoiWidget(config)
    layout.addWidget(self.roi_widget)

    layout.addWidget(QLabel('Frame Rate:'))
    self.framerate_spinbox = QDoubleSpinBox()
    self.framerate_spinbox.setRange(0, 1000000)
    self.framerate_spinbox.setSuffix('Hz')
    self.framerate_spinbox.editingFinished.connect(lambda: config.update({'AcquisitionFrameRate': self.framerate_spinbox.value()}))
    layout.addWidget(self.framerate_spinbox)

    layout.addWidget(QLabel('Exposure:'))
    self.exposure_spinbox = QDoubleSpinBox()
    self.exposure_spinbox.setRange(0, 1000000)
    self.exposure_spinbox.setSuffix('ms')
    self.exposure_spinbox.editingFinished.connect(lambda: config.update({'ExposureTime': 1000*self.exposure_spinbox.value()}))
    layout.addWidget(self.exposure_spinbox)

    layout.addWidget(QLabel('Gain:'))
    self.gain_spinbox = QDoubleSpinBox()
    self.gain_spinbox.setRange(0, 1000000)
    self.gain_spinbox.editingFinished.connect(lambda: config.update({'Gain': self.gain_spinbox.value()}))
    layout.addWidget(self.gain_spinbox)

    streaming = False
    async def toggle_stream():
      name = self.config['name']
      if streaming:
        response = await self.stub.node_request(thalamus_pb2.NodeRequest(node=name,json="\"stop_stream\""))
        self.streaming = False
        self.stream_button.setText('Start')
      else:
        response = await self.stub.node_request(thalamus_pb2.NodeRequest(node=name,json="\"start_stream\""))
        self.streaming = True
        self.stream_button.setText('Stop')

    def sync_toggle_stream():
      create_task_with_exc_handling(toggle_stream())

    self.stream_button = QPushButton('Start')
    self.stream_button.clicked.connect(sync_toggle_stream)
    #layout.addWidget(self.stream_button)

    layout.addStretch(1)

    self.setLayout(layout)

    for k, v in self.config.items():
      self.on_change(None, k, v)
                                          
  def on_change(self, action, key, value):
    if key == 'Running':
      if self.running_checkbox.isChecked() != value:
        self.running_checkbox.setChecked(value)
    elif key == 'Camera':
      self.camera_combobox.setCurrentText(value)
    elif key == 'AcquisitionFrameRate':
      if abs(self.framerate_spinbox.value() - value) >= 1:
        self.framerate_spinbox.setValue(value)
    elif key == 'ExposureTime':
      if abs(1000*self.exposure_spinbox.value() - value) >= 1:
        self.exposure_spinbox.setValue(.001*value)
    elif key == 'Gain':
      if abs(self.gain_spinbox.value() - value) >= 1:
        self.gain_spinbox.setValue(value)

