import collections
import functools
import bisect
import typing
import bisect
import pdb

from ..qt import *
from ..config import *

class FlatObservableCollectionModel(QAbstractItemModel):
  def __init__(self, config: ObservableCollection, transformer: typing.Callable[[typing.Any], typing.Any], depth = 1):
    self.config = config
    self.transformer = transformer
    self.values = []

    self.config.add_observer(functools.partial(self.__on_change, depth))

  def __update(self):
    items = self.config.values() if isinstance(self.config, ObservableDict) else self.config
    new_values = sorted(self.transformer(i) for i in items)

    i, j = 0, 0

    while i < len(self.values) and j < len(new_values):
      if self.values[i] < new_values[j]:
        self.beginRemoveRows(QModelIndex(), i, i)
        del self.values[i]
        self.endRemoveRows()
      elif self.values[i] > new_values[j]:
        self.beginInsertRows(QModelIndex(), i, i)
        self.values.insert(i, new_values[j])
        self.endInsertRows()
        i += 1
        j += 1
      else:
        i += 1
        j += 1

    if i < len(self.values):
      self.beginRemoveRows(QModelIndex(), i, len(self.values)-1)
      del self.values[i:]
      self.endRemoveRows()
    if j < len(new_values):
      self.beginInsertRows(QModelIndex(), len(self.values), len(self.values) + (len(new_values) - j) - 1)
      self.values.extend(new_values[j:])
      self.endInsertRows()

  def __on_change(self, remaining_levels: int, action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
    if action == ObservableCollection.Action.SET:
      if isinstance(value, ObservableCollection):
        if remaining_levels:
          value.add_observer(functools.partial(self.__on_change, remaining_levels-1))
    self.__update()

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    #print('data', index.row(), index.column(), role)
    if not index.isValid():
      return None

    if role != Qt.ItemDataRole.DisplayRole:
      return super().data(index, role)

    return self.values[index.row()]

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)

    self.values[index.row()] = value

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    #print('index', row, column, parent, self.hasIndex(row, column, parent))
    if not self.hasIndex(row, column, parent):
      return QModelIndex()

    return self.createIndex(row, column, parent)
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    #print('parent', index)
    return QModelIndex()

  def rowCount(self, parent: QModelIndex) -> int:
    return len(self.values)

  def columnCount(self, _: QModelIndex) -> int:
    return 1

class ObservableCollectionModel(QAbstractItemModel):
  def __init__(self, config: ObservableCollection, columns: typing.List[str] = [], just_columns: bool = False):
    self.config = config
    self.columns = columns
    self.just_columns = just_columns
    self.prefix_columns = 1 if just_columns else 2

    self.item_to_keys = {id(config): []}
    self.item_to_index = {id(config): QModelIndex()}
    self.index_to_item = {QModelIndex(): config}

    config.add_recursive_observer(self.__on_change, lambda: isdeleted(self))
    config.recap(functools.partial(self.__on_change, config))

  def __on_change(self, source: ObservableCollection, action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
    if not source.is_descendent(self.config):
      return

    index = self.item_to_index[id(source)]
    keys = self.item_to_keys[id(source)]

    is_column = key in self.columns
    if action == ObservableCollection.Action.SET:
      if is_column:
        j = bisect.bisect_left(self.columns, key)
        value_index = self.index(0, self.prefix_columns+j, index)
        self.dataChanged.emit(value_index, value_index)
        return

      i = bisect.bisect_left(keys, key)

      if isinstance(value, ObservableCollection):
        value_index = self.index(i, 0, index)
        self.item_to_keys[id(value)] = []
        self.item_to_index[id(value)] = value_index
        self.index_to_item[value_index] = value
       

      if i < len(keys) and key == keys[i]:
        key_index = self.index(i, 0, index)
        value_index = self.index(i, self.prefix_columns, index)
        self.dataChanged.emit(key_index, value_index)
      else:
        self.beginInsertRows(index, i, i)
        keys.insert(i, key)
        self.endInsertRows()

      if isinstance(value, ObservableCollection):
        value.recap(functools.partial(self.__on_change, value))

    else:
      i = bisect.bisect_left(keys, key)

      if isinstance(value, ObservableCollection):
        value_index = self.index(i, 0, index)
        del self.item_to_keys[id(value)]
        del self.item_to_index[id(value)]
        del self.index_to_item[value_index]

      if is_column:
        j = bisect.bisect_left(self.columns, key)
        value_index = self.index(0, self.prefix_columns+j, index)
        self.dataChanged.emit(value_index, value_index)
      else:
        self.beginRemoveRows(index, i, i)
        del keys[i]
        self.endRemoveRows()

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    #print('data', index.row(), index.column(), role)
    if not index.isValid():
      return None

    if role != Qt.ItemDataRole.DisplayRole:
      return super().data(index, role)

    item = self.index_to_item[index]
    if index.column() >= self.prefix_columns:
      key = self.columns[index.column()-self.prefix_columns]
      value = item.get(key, None)
      if role == Qt.ItemDataRole.CheckStateRole:
        return Qt.CheckState.Checked if value else Qt.CheckState.Unchecked
      return value

    keys = self.item_to_keys[id(item)]
    key = keys[index.row()]
    if index.column() == 0:
      return key

    value = item.get(key, None)
    if role == Qt.ItemDataRole.CheckStateRole:
      return Qt.CheckState.Checked if value else Qt.CheckState.Unchecked
    if isinstance(value, ObservableCollection):
      return ''
    return value

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)

    if index.column() == 0:
      return False

    item = self.index_to_item[index]
    if index.column() >= self.prefix_columns:
      key = self.columns[index.column()-self.prefix_columns]
    else:
      keys = self.item_to_keys[id(item)]
      key = keys[index.row()]

    if value == Qt.CheckState.Checked:
      value = True
    if value == Qt.CheckState.Unchecked:
      value = False

    item[key] = value
    return True

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    #print('flags', index)
    flags = super().flags(index)
    if index.column() > 0:
      flags |= Qt.ItemFlag.ItemIsEditable

    item = self.index_to_item[index]
    if index.column() >= self.prefix_columns:
      key = self.columns[index.column()-self.prefix_columns]
    else:
      keys = self.item_to_keys[id(item)]
      key = keys[index.row()]

    value = item.get(key, None)
    if isinstance(value, bool):
      flags |= Qt.ItemFlag.ItemIsUserCheckable

    return flags

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if self.just_columns:
        return self.columns[section-self.prefix_columns]
      else:
        if section == 0:
          return "Key"
        elif section == 1:
          return "Value"
        else:
          return self.columns[section-self.prefix_columns]

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    #print('index', row, column, parent, self.hasIndex(row, column, parent))
    if not self.hasIndex(row, column, parent):
      return QModelIndex()

    return self.createIndex(row, column, parent)
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    #print('parent', index)
    if not index.isValid():
      return QModelIndex()

    item = self.index_to_item[index]
    if item.parent is None:
      return QModelIndex()
    parent_index = self.item_to_index[id(item.parent)]
    return parent_index

  def rowCount(self, parent: QModelIndex) -> int:
    #print('rowCount', parent.isValid(), self.sorted_keys)
    item = self.index_to_item[parent]
    keys = self.item_to_keys[id(item)]
    return len(keys)

  def columnCount(self, _: QModelIndex) -> int:
    #print('columnCount', _)
    return self.prefix_columns + len(self.columns)

class SyncWidget(QWidget):
  def __init__(self, config: ObservableDict, stub):
    super().__init__()
    self.config = config
    self.stub = stub
    assert self.config.parent is not None

    if 'Sources' not in self.config:
      self.config['Sources'] = {}
    sources = self.config['Sources']

    layout = QVBoxLayout()
    combo = QComboBox()
    combo_model = FlatObservableCollectionModel(self.config.parent, lambda c: c['name'])
    combo.setModel(combo_model)
    add_button = QPushButton('Add')
    qlist = QTreeView()
    model = ObservableCollectionModel(sources, ["Channel", "Is Sync"], True)
    qlist.setModel(model)
    #qlist.setItemDelegate(Delegate())
    remove_button = QPushButton('Remove')

    layout.addWidget(combo)
    layout.addWidget(add_button)
    layout.addWidget(qlist, 1)
    layout.addWidget(remove_button)

    self.setLayout(layout)

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

