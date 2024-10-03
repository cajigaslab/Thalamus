import functools
import typing
import bisect
import pdb

from ..qt import *
from ..config import *

class Delegate(QItemDelegate):
  def createEditor(self, parent: QWidget, option: QStyleOptionViewItem, index: QModelIndex) -> QWidget:
    if index.column() == 1:
      return QSpinBox(parent)
    elif index.column() == 2:
      return QLineEdit(parent)
    return super().createEditor(parent, option, index)
    
  def setEditorData(self, editor: QWidget, index: QModelIndex) -> None:
    if index.column() == 1:
      spinbox = typing.cast(QSpinBox, editor)
      spinbox.setValue(index.data())
      return
    elif index.column() == 2:
      edit = typing.cast(QLineEdit, editor)
      edit.setText(index.data())
      return
    return super().setEditorData(editor, index)
    
  def setModelData(self, editor: QWidget, model: QAbstractItemModel, index: QModelIndex):
    if index.column() == 1:
      spinbox = typing.cast(QSpinBox, editor)
      model.setData(index, spinbox.value())
    elif index.column() == 2:
      edit = typing.cast(QLineEdit, editor)
      model.setData(index, edit.text())
    return super().setEditorData(editor, index)

class SourcesModel(QAbstractItemModel):
  def __init__(self, config: ObservableDict, nodes: ObservableList):
    super().__init__()
    self.config = config
    self.nodes = nodes
    self.config.add_observer(self.on_sources_change, functools.partial(isdeleted, self))
    self.sorted_keys = []
    self.monitored_nodes = set()
    for k, v in self.config.items():
      self.on_sources_change(ObservableCollection.Action.SET, k, v)

  def num_channels(self):
    result = 0
    for key, value in self.config.items():
      result += len(value)
    return result

  def get_mappings(self, index: QModelIndex):
    if index.parent().isValid():
      index = index.parent()

    print('get_mappings', self.sorted_keys, index.row())
    key = self.sorted_keys[index.row()]
    return self.config[key]

  def get_row(self, key):
    return bisect.bisect_left(self.sorted_keys, key)

  def on_sources_change(self, action, key, value):
    if action == ObservableCollection.Action.SET:
      i = bisect.bisect_left(self.sorted_keys, key)
      self.beginInsertRows(QModelIndex(), i, i)
      self.sorted_keys.insert(i, key)
      print('on_sources_change', self.sorted_keys)
      self.endInsertRows()
      value.add_observer(lambda *args: self.on_mappings_change(key, *args), functools.partial(isdeleted, self))
      for k, v in enumerate(value):
        self.on_mappings_change(key, ObservableCollection.Action.SET, k, v)

      def on_name_change(a, k, v):
        if k == 'name' and key in self.config:
          mappings = self.config[key]
          del self.config[key]
          self.config[v] = mappings

      for node in self.nodes:
        if node['name'] == key and id(node) not in self.monitored_nodes:
          node.add_observer(on_name_change, functools.partial(isdeleted, self))
          self.monitored_nodes.add(id(node))
    else:
      print('on_sources_change, remove')
      i = bisect.bisect_left(self.sorted_keys, key)
      self.beginRemoveRows(QModelIndex(), i, i)
      del self.sorted_keys[i]
      self.endRemoveRows()
    pass

  def on_mappings_change(self, i_name, action, key, value):
    i = self.get_row(i_name)
    print('on_mappings_change', i, action, key, value)
    parent = self.index(i, 0, QModelIndex())
    if action == ObservableCollection.Action.SET:
      self.beginInsertRows(parent, key, key)
      self.endInsertRows()
      value.add_observer(lambda *args: self.on_mapping_change(value, i_name, key, *args), functools.partial(isdeleted, self))
      for k, v in value.items():
        self.on_mapping_change(value, i_name, key, ObservableCollection.Action.SET, k, v)
    else:
      self.beginRemoveRows(parent, key, key)
      self.endRemoveRows()

  def on_mapping_change(self, self_channel, i_name, j, action, key, value):
    i = self.get_row(i_name)
    print('on_mapping_change', i, j, action, key, value, self.config)
    parent = self.index(i, 0, QModelIndex())
    self.dataChanged.emit(self.index(j, 0, parent), self.index(j, 1, parent))
    used_channels = set()
    conflict = None
    for n, channels in self.config.items():
      for c in channels:
        i = c['Out Channel']
        used_channels.add(i)
        if i == value and self_channel is not c:
          conflict = c
    if conflict:
      i = 0
      while i in used_channels:
        i += 1
      conflict['Out Channel'] = i

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    #print('data', index.row(), index.column(), role)
    if not index.isValid():
      return None

    if role != Qt.ItemDataRole.DisplayRole:
      return None

    collection = index.internalPointer()
    if collection is self.config:
      #print('data3', collection, self.sorted_keys, self.sorted_keys[index.row()])
      if index.column() == 0:
        return self.sorted_keys[index.row()]
      else:
        return ''
    else:
      node = index.internalPointer()
      row = node[index.row()]
      #print('data2', node, row)
      if index.column() == 0:
        return f'{index.row()}: {row["Name"]}'
      elif index.column() == 1:
        return row["Out Channel"]
      elif index.column() == 2:
        return row.get("Out Name", '')
      else:
        return None

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)

    collection = index.internalPointer()
    if collection is not self.config:
      node = index.internalPointer()
      row = node[index.row()]

      if index.column() == 1:
        row["Out Channel"] = value
        self.dataChanged.emit(index, index, [role])
        return True
      elif index.column() == 2:
        row["Out Name"] = value
        self.dataChanged.emit(index, index, [role])
        return True
    return False

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    #print('flags', index)
    flags = super().flags(index)
    if not index.isValid():
      return flags

    collection = index.internalPointer()
    if collection is not self.config and index.column() > 0:
      flags |= Qt.ItemFlag.ItemIsEditable
    return flags

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Input"
      elif section == 1:
        return "Channel"
      elif section == 2:
        return "Name"
    return None

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    #print('index', row, column, parent, self.hasIndex(row, column, parent))
    if not self.hasIndex(row, column, parent):
      return QModelIndex()

    if not parent.isValid():
      if row < len(self.config):
        return self.createIndex(row, column, self.config)
    else:
      key = self.sorted_keys[parent.row()]
      node = self.config[key]
      if row < len(node):
        return self.createIndex(row, column, node)
    return QModelIndex()
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    #print('parent', index)
    if not index.isValid():
      return QModelIndex()

    collection = index.internalPointer()
    if collection is self.config:
      return QModelIndex()

    for i, key in enumerate(self.sorted_keys):
      node = self.config[key]
      if collection is node:
        return self.createIndex(i, 0, self.config)
      
    raise RuntimeError("Failed to find index of node")

  def rowCount(self, parent: QModelIndex) -> int:
    #print('rowCount', parent.isValid(), self.sorted_keys)
    if not parent.isValid():
      #print('rowCount2', len(self.sorted_keys))
      return len(self.sorted_keys)
    else:
      collection = parent.internalPointer()
      if collection is self.config:
        key = self.sorted_keys[parent.row()]
        return len(self.config[key])
      else:
        return 0

  def columnCount(self, _: QModelIndex) -> int:
    #print('columnCount', _)
    return 3

class ChannelPickerWidget(QWidget):
  def __init__(self, config: ObservableDict, stub):
    super().__init__()
    self.config = config
    self.stub = stub

    if 'Sources' not in self.config:
      self.config['Sources'] = {}
    sources = self.config['Sources']

    layout = QVBoxLayout()
    combo = QComboBox()
    add_button = QPushButton('Add')
    qlist = QTreeView()
    model = SourcesModel(sources, config.parent)
    qlist.setModel(model)
    qlist.setItemDelegate(Delegate())
    remove_button = QPushButton('Remove')

    layout.addWidget(combo)
    layout.addWidget(add_button)
    layout.addWidget(qlist, 1)
    layout.addWidget(remove_button)

    self.setLayout(layout)

    nodes = config.parent
    assert nodes is not None, "nodes list not found"
    for target_node in sorted(nodes, key=lambda n: n['name']):
      if target_node is self.config:
        continue
      name = target_node['name']
      combo.addItem(name, target_node)

    def on_add():
      new_node = combo.currentData()
      new_name = new_node['name']
      if new_name in sources:
        return
      sources[new_name] = []
    add_button.clicked.connect(on_add)

    def on_remove():
      for item in qlist.selectedIndexes():
        if item.parent().isValid():
          item = item.parent()
        del sources[item.data()]
    remove_button.clicked.connect(on_remove)

