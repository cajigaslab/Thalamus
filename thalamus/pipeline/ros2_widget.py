import functools
import typing
import bisect
import pdb

from ..qt import *
from ..config import *

class Delegate(QItemDelegate):
  def createEditor(self, parent: QWidget, option: QStyleOptionViewItem, index: QModelIndex) -> QWidget:
    if index.column() == 4:
      return QSpinBox(parent)
    return super().createEditor(parent, option, index)
    
  def setEditorData(self, editor: QWidget, index: QModelIndex) -> None:
    if index.column() == 4:
      spinbox = typing.cast(QSpinBox, editor)
      spinbox.setValue(index.data())
      return
    return super().setEditorData(editor, index)
    
  def setModelData(self, editor: QWidget, model: QAbstractItemModel, index: QModelIndex):
    if index.column() == 4:
      spinbox = typing.cast(QSpinBox, editor)
      model.setData(index, spinbox.value())
    return super().setModelData(editor, model, index)

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

  def get_row(self, key):
    return bisect.bisect_left(self.sorted_keys, key)

  def on_sources_change(self, action, key, value):
    if action == ObservableCollection.Action.SET:
      i = bisect.bisect_left(self.sorted_keys, key)
      self.beginInsertRows(QModelIndex(), i, i)
      self.sorted_keys.insert(i, key)
      print('on_sources_change', self.sorted_keys)
      self.endInsertRows()
      value.add_observer(lambda *args: self.on_source_change(key, *args), functools.partial(isdeleted, self))
      for k, v in enumerate(value):
        self.on_source_change(key, ObservableCollection.Action.SET, k, v)

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

  def on_source_change(self, i_name, action, key, value):
    i = self.get_row(i_name)
    self.dataChanged.emit(self.index(i, 0, QModelIndex()), self.index(i, 4, QModelIndex()))

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    #print('data', index.row(), index.column(), role)
    if not index.isValid():
      return None

    if role != Qt.ItemDataRole.DisplayRole:
      return None

    key = self.sorted_keys[index.row()]
    row_data = self.config[key]
    if index.column() == 0:
      return key
    elif index.column() == 1:
      return row_data['Image Topic']
    elif index.column() == 2:
      return row_data['Camera Info Topic']
    elif index.column() == 3:
      return row_data['Gaze Topic']
    elif index.column() == 4:
      return row_data['Eye']
    elif index.column() == 5:
      return row_data['Parent Frame']
    elif index.column() == 6:
      return row_data['Child Frame']

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)

    key = self.sorted_keys[index.row()]
    row_data = self.config[key]
    if index.column() == 1:
      row_data['Image Topic'] = value
      return True
    elif index.column() == 2:
      row_data['Camera Info Topic'] = value
      return True
    elif index.column() == 3:
      row_data['Gaze Topic'] = value
      return True
    elif index.column() == 4:
      row_data['Eye'] = value
      return True
    elif index.column() == 5:
      row_data['Parent Frame'] = value
      return True
    elif index.column() == 6:
      row_data['Child Frame'] = value
      return True
    return False

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    #print('flags', index)
    flags = super().flags(index)
    if not index.isValid():
      return flags

    if index.column() > 0:
      flags |= Qt.ItemFlag.ItemIsEditable
    return flags

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Node"
      elif section == 1:
        return "Image Topic"
      elif section == 2:
        return "Camera Info Topic"
      elif section == 3:
        return "Gaze Topic"
      elif section == 4:
        return "Eye"
      elif section == 5:
        return "Parent Frame"
      elif section == 6:
        return "Child Frame"
    return None

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    if not self.hasIndex(row, column, parent):
      return QModelIndex()
    return self.createIndex(row, column, None)
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    return QModelIndex()

  def rowCount(self, parent: QModelIndex) -> int:
    if not parent.isValid():
      return len(self.sorted_keys)
    return 0

  def columnCount(self, parent: QModelIndex) -> int:
    if not parent.isValid():
      return 7
    return 0

class Ros2Widget(QWidget):
  def __init__(self, config: ObservableDict, stub):
    super().__init__()
    self.config = config
    self.stub = stub

    if 'Sources' not in self.config:
      self.config['Sources'] = {}
    sources = self.config['Sources']
    for source in sources:
      if 'Parent Frame' not in source:
        source['Parent Frame'] = ''
      if 'Child Frame' not in source:
        source['Child Frame'] = ''

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
      sources[new_name] = {'Image Topic': '', 'Camera Info Topic': '', 'Gaze Topic': '', 'Eye': 0, 'Parent Frame': '', 'Child Frame': ''}
    add_button.clicked.connect(on_add)

    def on_remove():
      for item in qlist.selectedIndexes():
        if item.parent().isValid():
          item = item.parent()
        del sources[item.data()]
    remove_button.clicked.connect(on_remove)

