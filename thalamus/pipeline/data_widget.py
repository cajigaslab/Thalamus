import typing
import asyncio
import numpy
import math
import grpc
import bisect
import pathlib
import datetime
import itertools
import traceback
import matplotlib
from ..config import *

from ..util import open_preferred_app
from ..util import MeteredUpdater
from .. import thalamus_pb2

from ..qt import *

from ..task_controller.util import create_task_with_exc_handling

class DataWidget(QMainWindow):
  def __init__(self, config, root_config, stub):
    super().__init__()
    self.root_config = root_config
    self.config = config
    self.stub = stub

    if 'view_geometry' not in config:
      config['view_geometry'] = [100, 100, 400, 400]
    if 'rows' not in config:
      config['rows'] = 1
    if 'columns' not in config:
      config['columns'] = 1
    if 'views' not in config:
      config['views'] = [{'row': 0, 'column': 0}]
    self.view_geometry_updater = MeteredUpdater(config['view_geometry'], datetime.timedelta(seconds=1), lambda: isdeleted(self))

    menubar = self.menuBar()
    fileMenu = menubar.addMenu('File')
    fileMenu.addAction('Save Screenshot').triggered.connect(self.on_screenshot)
    viewMenu = menubar.addMenu('View')
    viewMenu.addAction('Add Row').triggered.connect(lambda: config.update({'rows': config['rows']+1}))
    viewMenu.addAction('Add Column').triggered.connect(lambda: config.update({'columns': config['columns']+1}))

    x, y, w, h = config['view_geometry']
    self.move(x, y)
    self.resize(w, h)

    self.running = False

    self.grid = QGridLayout()
    central_widget = QWidget()
    central_widget.setLayout(self.grid)
    self.setCentralWidget(central_widget)

    self.show()

    config.add_observer(self.on_config_change)
    config['views'].add_observer(self.on_views_change)
    for i, v in enumerate(config['views']):
      self.on_views_change(ObservableCollection.Action.SET, i, v)

  def on_screenshot(self):
    widget = self.centralWidget()
    image = QImage(widget.width(), widget.height(), QImage.Format.Format_RGB32) # type: ignore # pylint: disable=no-member
    self.centralWidget().render(image)

    i = 1
    time_string = datetime.datetime.now().strftime('%Y%m%dT%H%M%S')
    path = pathlib.Path.home() / f'thalamus-screenshot-{time_string}.png'
    while path.exists():
      i += 1
      path = pathlib.Path.home() / f'thalamus-screenshot-{time_string}-{i}.png'
    image.save(str(path))
    open_preferred_app(path)

  def on_config_change(self, action, key, value):
    if key == 'rows':
      for column in range(self.config['columns']):
        self.config['views'].append({'row': value-1, 'column': column})
    elif key == 'columns':
      for row in range(self.config['rows']):
        self.config['views'].append({'row': row, 'column': value-1})

  def on_views_change(self, action, key, value):
    if ObservableCollection.Action.SET:
      self.grid.addWidget(Plot(value, self.root_config, self.stub), value['row'], value['column'])

  def moveEvent(self, a0: QMoveEvent) -> None:
    offset = self.frameGeometry().size() - self.geometry().size()
    position = a0.pos() - QPoint(offset.width(), offset.height())
    position = QPoint(max(0, position.x()), max(0, position.y()))
    self.view_geometry_updater[:2] = position.x(), position.y()
    return super().moveEvent(a0)

  def resizeEvent(self, a0: QResizeEvent) -> None:
    self.view_geometry_updater[2:] = a0.size().width(), a0.size().height()
    return super().resizeEvent(a0)

  def closeEvent(self, a0: QCloseEvent) -> None:
    for i, view in enumerate(self.config.parent):
      if view is self.config:
        del self.config.parent[i]
        break
    return super().closeEvent(a0)

class PlotCanvas(QWidget):
  def __init__(self, stub, config):
    super().__init__()
    self.stub = stub
    self.task = None
    self.config = config

    if 'draw_value' not in self.config:
      config['draw_value'] = False
    if 'static_range' not in self.config:
      config['static_range'] = False
    if 'range_min' not in self.config:
      config['range_min'] = 0
    if 'range_max' not in self.config:
      config['range_max'] = 10

    self.name = ""
    self.bin_ns = int(10e9/1920)
    self.current_ns = 0
    self.ydata = []
    self.paths: typing.List[QPainterPath] = [QPainterPath(), QPainterPath()]
    self.offset_ns = 0
    self.duration_ns = 10e9
    self.range = math.inf, -math.inf
    self.position = []
    self.linspace = numpy.linspace(0, 10, 2*1920)
    self.draw_value = False
    self.current_value = 0.0

    config.add_observer(self.on_change, lambda: isdeleted(self))

    for k, v in self.config.items():
      self.on_change(ObservableCollection.Action.SET, k, v)

  def on_change(self, a, k, v):
    if k == 'draw_value':
      self.draw_value = v
    elif k == 'static_range':
      self.range = math.inf, -math.inf
    self.update()

  def restart(self):
    self.stop()
    self.start()

  def start(self):
    if self.task is None:
      self.toggle()

  def stop(self):
    print('stop', self.task)
    if self.task is not None:
      self.toggle()

  def toggle(self):
    if self.task is not None:
      print('Will Cancel')
      self.stream.cancel()
      self.task.cancel()
      self.task = None
      return

    #traceback.print_stack()
    request = thalamus_pb2.GraphRequest(
      node = thalamus_pb2.NodeSelector(name = self.config["selected_node"]),
      bin_ns = self.bin_ns,
      channel_names = [self.config['selected_channel']]
    )
    self.stream = stream = self.stub.graph(request)

    self.task = create_task_with_exc_handling(self.__stream_task(stream))


  def paintEvent(self, event):
    super().paintEvent(event)
    
    if not self.config['static_range']:
      if self.paths[0].isEmpty() and self.paths[1].isEmpty():
        range_size = 1
      else:
        range_size = 0
        if not self.paths[0].isEmpty():
          bounds = self.paths[0].boundingRect()
          range_size = max(range_size, bounds.height())
          self.range = min(bounds.y(), self.range[0]), max(bounds.y() + bounds.height(), self.range[1])

        if not self.paths[1].isEmpty():
          bounds = self.paths[1].boundingRect()
          range_size = max(range_size, bounds.height())
          self.range = min(bounds.y(), self.range[0]), max(bounds.y() + bounds.height(), self.range[1])

      if range_size == 0:
        range_size = 1
      range = self.range[0] - range_size/10, self.range[1] + range_size/10
    else:
      self.range = self.config['range_min'], self.config['range_max']
      range_size = self.range[1] - self.range[0]
      if range_size <= 0:
        range_size = 1
      range = self.config['range_min'] - range_size/10, self.config['range_max'] + range_size/10

    painter = QPainter(self)
    metrics = QFontMetrics(painter.font())

    range_size = range[1] - range[0]
    if range_size == 0:
      range_size = 1
    
    pen = painter.pen()
    pen.setCosmetic(True)
    vertical_scale = (self.height() - 2*metrics.height())/range_size
    painter.translate(metrics.height(), self.height() + range[0]*vertical_scale - metrics.height())
    painter.scale((self.width() - 2*metrics.height())/self.duration_ns, -vertical_scale)
    offset = -self.current_ns
    painter.save()
    painter.setClipRect(QRectF(0, range[0], self.duration_ns, range_size))
    for path, color in zip(self.paths, [Qt.GlobalColor.blue, Qt.GlobalColor.blue, Qt.GlobalColor.blue]):
      pen.setColor(color)
      painter.setPen(pen)
      painter.save()
      painter.translate(offset, 0)
      painter.drawPath(path)
      painter.restore()
      offset += self.duration_ns
    painter.restore()

    pen.setColor(Qt.GlobalColor.black)
    painter.setPen(pen)
    bounds = QRectF(0, range[0], self.duration_ns, range_size)
    device_bounds = painter.transform().mapRect(bounds)
    painter.drawRect(bounds)

    painter.resetTransform()

    if self.draw_value and not math.isnan(device_bounds.height()):
      denom = self.range[1] - self.range[0]
      denom = denom if denom > 0 else 1
      interp = (self.current_value - self.range[0])/denom
      interp = max(0.0, min(1.0, interp))
      text = "{0:.3}".format(self.current_value)
      font = painter.font()
      font.setPixelSize(int(device_bounds.height()/2))
      painter.save()
      painter.setPen(QColor(int(255*(1-interp)), 0, int(255*interp)))
      painter.setFont(font)
      painter.drawText(device_bounds, Qt.AlignmentFlag.AlignRight, text)
      painter.restore()

    painter.drawText(0, metrics.height(), str(self.range[1]))
    painter.drawText(0, self.height(), str(self.range[0]))

    name_bounds = metrics.boundingRect(self.name)
    painter.drawText(self.width() - name_bounds.width(), self.height(), self.name)

  def closeEvent(self, e):
    self.stop()

  async def __stream_task(self, stream: typing.AsyncIterable[thalamus_pb2.GraphResponse]):
    try:
      async for response in stream:
        looped = False
        if not len(response.bins):
          continue
        for i, span in enumerate(response.spans):
          self.name = span.name
          for value in response.bins[span.begin:span.end]:
            if self.current_ns >= self.duration_ns:
              self.paths = [self.paths[1], QPainterPath()]
              self.range = math.inf, -math.inf
              self.current_ns = 0

            if self.paths[1].elementCount() == 0:
              self.paths[1].moveTo(self.current_ns, value)
            else:
              self.paths[1].lineTo(self.current_ns, value)
            self.current_ns += self.bin_ns/2
            self.current_value = value

        self.update()
    except grpc.aio.AioRpcError:
      pass
    except asyncio.CancelledError:
      pass
    finally:
      print('Cancelling')
      stream.cancel()
      print('Cancelled')


class SpectrogramCanvas(QWidget):
  def __init__(self, stub, config):
    super().__init__()
    self.stub = stub
    self.task = None
    self.config = config
    if 'static_range' not in self.config:
      config['static_range'] = False
    if 'range_min' not in self.config:
      config['range_min'] = 0
    if 'range_max' not in self.config:
      config['range_max'] = 10

    self.max_frequency = 1
    self.time = 0
    self.image_floats = None
    self.image_bytes = None
    self.qimage = None

    config.add_observer(self.on_change, lambda: isdeleted(self))

    for k, v in self.config.items():
      self.on_change(ObservableCollection.Action.SET, k, v)

  def on_change(self, a, k, v):
    self.update()

  def restart(self):
    self.stop()
    self.start()

  def start(self):
    if self.task is None:
      self.toggle()

  def stop(self):
    if self.task is not None:
      self.toggle()

  def toggle(self):
    if self.task is not None:
      self.task.cancel()
      self.task = None
      return

    request = thalamus_pb2.SpectrogramRequest(
      node = thalamus_pb2.NodeSelector(name = self.config["selected_node"]),
      channels = [thalamus_pb2.ChannelId(name=self.config['selected_channel'])],
      window_s = .25,
      hop_s = .125,
    )
    stream = self.stub.spectrogram(request)
    self.path = QPainterPath()

    self.task = create_task_with_exc_handling(self.__stream_task(stream))

  def paintEvent(self, event):
    super().paintEvent(event)
    painter = QPainter(self)
    metrics = QFontMetrics(painter.font())

    if self.qimage is not None:
      if self.config['static_range']:
        lower, upper = self.config['range_min']/self.max_frequency, self.config['range_max']/self.max_frequency
      else:
        lower, upper = 0, 1

      painter.drawText(0, metrics.height(), str(upper*self.max_frequency))
      painter.drawText(0, self.height(), str(lower*self.max_frequency))

      source_rect = QRect(0, int((1 - upper)*self.qimage.height()), self.qimage.width(), int((upper-lower)*self.qimage.height()))
      painter.drawImage(QRect(0, metrics.height(), self.width(), self.height() - 2*metrics.height()), self.qimage, source_rect)

  async def __stream_task(self, stream: typing.AsyncIterable[thalamus_pb2.GraphResponse]):
    try:
      async for response in stream:
        if len(response.spectrograms) == 0:
          break
        spectrogram = response.spectrograms[0]
        last_max_frequency, self.max_frequency = self.max_frequency, spectrogram.max_frequency
        #print(self.max_frequency)
        width, height = 20, len(spectrogram.data)//2
        if self.image_floats is None or last_max_frequency != self.max_frequency:
          self.image_floats = numpy.zeros((height, width))
          self.image_bytes = numpy.zeros((height, width))

        for i in range(0, len(spectrogram.data), 2):
          real, imag = spectrogram.data[i], spectrogram.data[i+1]
          self.image_floats[-1-i//2,self.time] = math.sqrt(real**2 + imag**2)
        self.time = (self.time + 1) % 20

        min, max = self.image_floats.min(), self.image_floats.max()
        #print((self.image_floats - min)/(max-min))
        self.image_bytes = ((self.image_floats - min)/(max-min)*255).astype(numpy.uint8)
        #print(self.image_bytes.max(), min, max)
        #print(self.image_bytes)

        self.qimage = QImage(self.image_bytes.tobytes(), width, height,
                                 width,
                                 QImage.Format.Format_Indexed8)
        colormap_raw = matplotlib.cm.viridis(range(256))
        colormap = [QColor.fromRgbF(c[0], c[1], c[2]).rgb() for c in colormap_raw.tolist()]
        self.qimage.setColorTable(colormap)

        self.update()
    except asyncio.CancelledError:
      pass
    finally:
      print('Cancelling')
      stream.cancel()
      print('Cancelled')

class NodesModel(QAbstractListModel):
  def __init__(self, nodes_list):
    super().__init__()
    self.nodes_list = nodes_list
    self.nodes_list.add_observer(self.on_change, lambda: isdeleted(self))
    for i, n in enumerate(nodes_list):
      self.on_change(ObservableCollection.Action.SET, i, n)

  def on_change(self, action, key, value):
    print('NodesModel.on_change', action, key, value)
          
    if action == ObservableCollection.Action.SET:
      self.beginInsertRows(QModelIndex(), key, key)
      self.endInsertRows()

      def on_node_change(a2, k2, v2):
        index = self.index(key, 0, QModelIndex())
        self.dataChanged.emit(index, index) 

      value.add_observer(on_node_change, lambda: isdeleted(self))
    else:
      self.beginRemoveRows(QModelIndex(), key, key)
      self.endRemoveRows()

  def rowCount(self, parent):
    return len(self.nodes_list)

  def data(self, index, role = Qt.ItemDataRole.DisplayRole):
    if not index.isValid() or role != Qt.ItemDataRole.DisplayRole:
      return None
    return self.nodes_list[index.row()]['name']

class ChannelComboBox(QComboBox):
  def __init__(self, stub, config, selected_channel_key = 'selected_channel'):
    super().__init__()
    self.stub = stub
    self.config = config
    self.selected_channel_key = selected_channel_key 
    self.task: typing.Optional[asyncio.Task] = None
    self.config.add_observer(self.__on_change, lambda: isdeleted(self))
    for k, v in self.config.items():
      self.__on_change(None, k, v)

  def __on_change(self, action, key, value):
    print('ChannelComboBox.__on_change', action, key, value)
    if key == 'selected_node':
      if self.task is not None:
        self.task.cancel()
      self.task = create_task_with_exc_handling(self.__channel_info())

  async def __channel_info(self):
    self.clear()
    selected_node = self.config['selected_node']

    selector = thalamus_pb2.NodeSelector(name=selected_node)
    stream = self.stub.channel_info(thalamus_pb2.AnalogRequest(node=selector))
    try:
      async for message in stream:
        selected_channel = self.config[self.selected_channel_key]
        self.clear()
        self.addItems([s.name for s in message.spans])
        self.setCurrentText(selected_channel)
    except asyncio.CancelledError:
      pass
    except grpc.aio.AioRpcError as e:
      if e.code() != grpc.StatusCode.CANCELLED:
        raise

class AdvancedDialog(QDialog):
  def __init__(self, config):
    super().__init__()
    self.config = config
    if 'draw_value' not in self.config:
      config['draw_value'] = False
    if 'static_range' not in self.config:
      config['static_range'] = False
    if 'range_min' not in self.config:
      config['range_min'] = 0
    if 'range_max' not in self.config:
      config['range_max'] = 10
    if 'view_type' not in self.config:
      config['view_type'] = 'Time Series'

    self.draw_value = QCheckBox()
    self.draw_value.clicked.connect(lambda c: config.update({'draw_value': c}))

    self.static_range = QCheckBox()
    self.static_range.clicked.connect(lambda c: config.update({'static_range': c}))

    self.range_max = QDoubleSpinBox()
    self.range_max.setRange(-1e9, 1e9)
    self.range_max.valueChanged.connect(lambda v: config.update({'range_max': v}))
    self.range_min = QDoubleSpinBox()
    self.range_min.setRange(-1e9, 1e9)
    self.range_min.valueChanged.connect(lambda v: config.update({'range_min': v}))

    self.view_type = QComboBox()
    self.view_type.addItem('Time Series')
    self.view_type.addItem('Spectrogram')

    self.view_type.currentTextChanged.connect(lambda t: config.update({'view_type': t}))

    self.ok = QPushButton('Ok')
    self.ok.clicked.connect(lambda: self.accept())
    self.cancel = QPushButton('Cancel')

    layout = QGridLayout()
    layout.addWidget(QLabel('Draw Value'), 0, 0)
    layout.addWidget(self.draw_value, 0, 1, 1, 2)
    layout.addWidget(QLabel('Static Range'), 1, 0)
    layout.addWidget(self.static_range, 1, 1, 1, 2)
    layout.addWidget(QLabel('Range Min'), 2, 0)
    layout.addWidget(self.range_min, 2, 1, 1, 2)
    layout.addWidget(QLabel('Range Max'), 3, 0)
    layout.addWidget(self.range_max, 3, 1, 1, 2)
    layout.addWidget(QLabel('View Type'), 4, 0)
    layout.addWidget(self.view_type, 4, 1, 1, 2)
    layout.addWidget(self.ok, 5, 0, 1, 3)
    #layout.addWidget(self.cancel, 4, 2)
    self.setLayout(layout)

    config.add_observer(self.on_change, lambda: isdeleted(self))
    for k, v in self.config.items():
      self.on_change(None, k, v)

  def on_change(self, action, key, value):
    if key == 'draw_value':
      if value != self.draw_value.isChecked():
        self.draw_value.setChecked(value)
    elif key == 'static_range':
      self.range_min.setEnabled(value)
      self.range_max.setEnabled(value)
      if value != self.static_range.isChecked():
        self.static_range.setChecked(value)
    elif key == 'range_min':
      if value != self.range_min.value():
        self.range_min.setValue(float(value))
    elif key == 'range_max':
      if value != self.range_max.value():
        self.range_max.setValue(float(value))
    elif key == 'view_type':
      if value != self.view_type.currentText():
        self.view_type.setCurrentText(value)

class Plot(QWidget):
  def __init__(self, config: ObservableDict, root_config: ObservableDict, stub):
    super().__init__()
    self.config = config
    self.root_config = root_config
    self.stub = stub
    self.running = False
    nodes_model = NodesModel(root_config['nodes'])

    if 'selected_node' not in self.config:
      if nodes_model.rowCount(QModelIndex()):
        self.config['selected_node'] = nodes_model.data(nodes_model.index(0, 0))
      else:
        self.config['selected_node'] = ''
    if 'selected_channel' not in self.config:
      self.config['selected_channel'] = ''
    if 'view_type' not in self.config:
      self.config['view_type'] = 'Time Series'

    layout = QGridLayout()
    layout.setColumnStretch(0, 0)
    layout.setColumnStretch(1, 1)
    layout.setColumnStretch(2, 0)
    layout.setColumnStretch(3, 1)
    self.node_combobox = QComboBox()
    self.node_combobox.setModel(nodes_model)
    self.channel_combobox = ChannelComboBox(stub, config)
    self.channel_combobox.setEditable(True)
    advanced_button = QPushButton('...')
    self.canvas = PlotCanvas(stub, config)

    layout.addWidget(QLabel('Node:'), 0, 0)
    layout.addWidget(self.node_combobox, 0, 1)
    layout.addWidget(QLabel('Channel:'), 0, 2)
    layout.addWidget(self.channel_combobox, 0, 3)
    layout.addWidget(advanced_button, 0, 4)
    layout.addWidget(self.canvas, 1, 0, 1, 5)
    layout.setColumnStretch(1, 1)
    layout.setColumnStretch(3, 1)

    def on_advanced():
      dialog = AdvancedDialog(config)
      dialog.show()

    advanced_button.clicked.connect(on_advanced)

    self.setLayout(layout)
    self.grid_layout = layout

    assert self.root_config is not None
    self.nodes = self.root_config['nodes']
    print('__init__', self.config)

    def on_node_combobox_change(selected_node: str):
      print('on_node_combobox_change', selected_node, self.config['selected_node'])
      if selected_node == self.config['selected_node']:
        return
      self.config['selected_node'] = selected_node

    def on_channel_combobox_change(selected_channel: str):
      print('on_channel_combobox_change', selected_channel, self.config['selected_channel'])
      if selected_channel == self.config['selected_channel']:
        return
      self.config['selected_channel'] = selected_channel

    self.config.add_observer(self.on_config_changed, lambda: isdeleted(self))
    for k, v in self.config.items():
      self.on_config_changed(ObservableCollection.Action.SET, k, v)

    self.start()

    self.node_combobox.currentTextChanged.connect(on_node_combobox_change)
    self.channel_combobox.currentTextChanged.connect(on_channel_combobox_change)

  def on_config_changed(self, action, key, value):
    print(action, key, value)
    if key == 'selected_node':
      self.node_combobox.setCurrentText(value)
      self.canvas.restart()
    elif key == 'selected_channel':
      self.channel_combobox.setCurrentText(value)
      self.canvas.restart()
    elif key == 'view_type':
      widget_factory = PlotCanvas if value == 'Time Series' else SpectrogramCanvas
      new_canvas, old_canvas = widget_factory(self.stub, self.config), self.canvas
      old_canvas.stop()
      self.grid_layout.replaceWidget(old_canvas, new_canvas)
      self.canvas = new_canvas
      old_canvas.setParent(None)
      old_canvas.deleteLater()
      if self.running:
        self.canvas.start()

  def start(self):
    self.canvas.start()
    self.running = True

  def stop(self):
    self.canvas.stop()
    self.running = False

