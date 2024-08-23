from ..qt import *
import typing
import asyncio
import collections
from .. import thalamus_pb2
from ..task_controller.util import create_task_with_exc_handling

class ChannelsList(typing.NamedTuple):
  node: str
  channels: typing.List[str]
  sample_intervals: typing.List[int]


class ChannelsDataModel(QAbstractItemModel):
  def __init__(self, nodes, stub):
    super().__init__()
    self.nodes = nodes
    self.stub = stub
    self.analog_nodes: typing.List[ChannelsList] = []
    self.tasks = []
    create_task_with_exc_handling(self.update())

  def close(self):
    for task in self.tasks:
      task.cancel()

  async def get_channels(self, channels_list: ChannelsList, index: QModelIndex):
    selector = thalamus_pb2.NodeSelector(name=channels_list.node)
    stream = self.stub.channel_info(thalamus_pb2.AnalogRequest(node=selector))
    try:
      async for response in stream:
        if channels_list.channels:
          self.beginRemoveRows(index, 0, len(channels_list.channels)-1)
          channels_list.channels.clear()
          channels_list.sample_intervals.clear()
          self.endRemoveRows()

        new_channels = []
        new_sample_intervals = []
        for i in range(len(response.spans)):
          span = response.spans[i]
          sample_interval = response.sample_intervals[i]
          new_channels.append(span.name)
          new_sample_intervals.append(sample_interval)

        if new_channels:
          self.beginInsertRows(index, 0, len(new_channels)-1)
          channels_list.channels.extend(new_channels)
          channels_list.sample_intervals.extend(new_sample_intervals)
          self.endInsertRows()
    except asyncio.CancelledError:
      pass 

  async def update(self):
    for task in self.tasks:
      task.cancel()
    self.tasks = []

    if self.analog_nodes:
      self.beginRemoveRows(QModelIndex(), 0, len(self.analog_nodes) - 1)
      self.analog_nodes = []
      self.endRemoveRows()
    new_analog_nodes = []

    todo = []
    for node in sorted(self.nodes, key=lambda n: n['name']):
      name = node['name']

      selector = thalamus_pb2.NodeSelector(name=name)
      modalities = await self.stub.get_modalities(selector)
      if thalamus_pb2.Modalities.AnalogModality in modalities.values:
        channels_list = ChannelsList(name, [], [])
        new_analog_nodes.append(channels_list)
        todo.append(lambda row=len(new_analog_nodes)-1,channels_list=channels_list: create_task_with_exc_handling(self.get_channels(channels_list, self.index(row, 0, QModelIndex()))))

    if new_analog_nodes:
      self.beginInsertRows(QModelIndex(), 0, len(new_analog_nodes)-1)
      self.analog_nodes = new_analog_nodes
      self.endInsertRows()

    self.tasks = [t() for t in todo]


  def data(self, index: QModelIndex, role: int) -> typing.Any:
    if role != Qt.ItemDataRole.DisplayRole:
      return

    if not index.isValid():
      return None

    collection = index.internalPointer()
    if collection is self.analog_nodes:
      if index.column() == 0:
        row = self.analog_nodes[index.row()]
        return row.node
      else:
        return ''
    else:
      assert isinstance(collection, ChannelsList)
      if index.column() == 0:
        return f'{index.row()}: ' + collection.channels[index.row()]
      else:
        return 1e9/(collection.sample_intervals[index.row()] + 1e-100)

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Name"
      elif section == 1:
        return "Frequency"
    return None

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    #print('index', row, column, parent, self.hasIndex(row, column, parent))
    if not self.hasIndex(row, column, parent):
      return QModelIndex()

    if not parent.isValid():
      if row < len(self.analog_nodes):
        return self.createIndex(row, column, self.analog_nodes)
    else:
      channels_list = self.analog_nodes[parent.row()]
      if row < len(channels_list.channels):
        return self.createIndex(row, column, channels_list)
    return QModelIndex()
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    #print('parent', index)
    if not index.isValid():
      return QModelIndex()

    collection = index.internalPointer()
    if collection is self.analog_nodes:
      return QModelIndex()

    for i, node in enumerate(self.analog_nodes):
      if collection is node:
        return self.createIndex(i, 0, self.analog_nodes)
      
    raise RuntimeError("Failed to find index of node")

  def rowCount(self, parent: QModelIndex) -> int:
    #print('rowCount', parent.isValid(), self.sorted_keys)
    if not parent.isValid():
      #print('rowCount2', len(self.sorted_keys))
      return len(self.analog_nodes)
    else:
      collection = parent.internalPointer()
      if collection is self.analog_nodes:
        return len(self.analog_nodes[parent.row()].channels)
      else:
        return 0

  def columnCount(self, _: QModelIndex) -> int:
    #print('columnCount', _)
    return 2

class ChannelViewerWidget(QWidget):
  def __init__(self, nodes, stub):
    super().__init__()
    self.nodes = nodes
    self.stub = stub

    self.model = ChannelsDataModel(nodes, stub)
    
    layout = QVBoxLayout()
    refresh_button = QPushButton("Refresh")
    refresh_button.clicked.connect(lambda: create_task_with_exc_handling(self.model.update()) and None)

    tree = QTreeView()
    tree.setModel(self.model)

    layout.addWidget(refresh_button)
    layout.addWidget(tree)

    self.setLayout(layout)

  def closeEvent(self, e):
    self.model.close()

