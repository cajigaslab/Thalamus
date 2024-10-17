import pdb
import typing
import bisect
import functools
import traceback

from .config import ObservableCollection, ObservableDict, ObservableList
from .qt import *

class FlatObservableCollectionModel(QAbstractItemModel):
  def __init__(self, config: ObservableCollection, transformer: typing.Callable[[typing.Any], typing.Any], depth = 1):
    super().__init__()
    self.config = config
    self.transformer = transformer
    self.values = []

    self.config.add_observer(functools.partial(self.__on_change, depth))
    self.config.recap(functools.partial(self.__on_change, depth))

  def __update(self):
    print('__update')
    items = self.config.values() if isinstance(self.config, ObservableDict) else self.config
    new_values = sorted(self.transformer(i) for i in items)

    i, j = 0, 0

    while i < len(self.values) and j < len(new_values):
      print(i, j)
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
    if role == Qt.ItemDataRole.DisplayRole:
      return self.values[index.row()]
    elif role == Qt.ItemDataRole.EditRole:
      return self.values[index.row()]

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role == Qt.ItemDataRole.DisplayRole:
      self.values[index.row()] = value
      return True
    else:
      return super().setData(index, value, role)


  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    #print('index', row, column, parent, self.hasIndex(row, column, parent))
    return self.createIndex(row, column, parent)
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    #print('parent', index)
    return QModelIndex()

  def rowCount(self, parent: QModelIndex) -> int:
    return len(self.values)

  def columnCount(self, _: QModelIndex) -> int:
    return 1

class TreeObservableCollectionModel(QAbstractItemModel):
  def __init__(self, config: ObservableCollection, key_column: str = 'Key', value_column: str = 'Value',
               columns: typing.List[str] = [], show_extra_values: bool = True,
               is_editable: typing.Callable[[ObservableCollection, str], bool] = lambda a, b: False):
    super().__init__()
    self.config = config
    self.key_column = key_column
    self.value_column = value_column
    self.columns = columns
    self.show_extra_values = show_extra_values
    self.is_editable = is_editable
    self.prefix_columns = 2 if show_extra_values else 1
    self.index_cache = {}
    self.next_index_id = 1

    self.item_to_keys = {id(config): []}
    self.item_to_index = {id(config): QModelIndex()}
    self.index_to_item = {0: config}
    self.index_to_parent = {}

    config.add_recursive_observer(self.__on_change, lambda: isdeleted(self))
    config.recap(functools.partial(self.__on_change, config))

  def __on_change(self, source: ObservableCollection, action: ObservableCollection.Action, key: typing.Any, value: typing.Any, recursed = False):
    if not source.is_descendent(self.config):
      return

    index = self.item_to_index[id(source)]
    keys = self.item_to_keys[id(source)]

    is_column = key in self.columns
    if action == ObservableCollection.Action.SET:
      if is_column and source is not self.config:
        #pdb.set_trace()
        j = bisect.bisect_left(self.columns, key)
        key_in_parent = source.key_in_parent()
        parent_keys = self.item_to_keys[id(source.parent)]
        k = bisect.bisect_left(parent_keys, key_in_parent)
        value_index = self.index(k, self.prefix_columns+j, index.parent())
        self.dataChanged.emit(value_index, value_index)
        return

      i = bisect.bisect_left(keys, key)

      if isinstance(value, ObservableCollection):
        if i < len(keys) and key == keys[i]:
          self.beginRemoveRows(index, i, i)
          del keys[i]
          self.endRemoveRows()

        value_index = self.index(i, 0, index)
        self.item_to_keys[id(value)] = []
        self.item_to_index[id(value)] = value_index
        self.index_to_item[value_index.internalId()] = value

        self.beginInsertRows(index, i, i)
        keys.insert(i, key)
        self.endInsertRows()
      elif self.show_extra_values:
        if i < len(keys) and key == keys[i]:
          value_index = self.index(i, 1, index)
          self.dataChanged.emit(value_index, value_index)
        else:
          self.beginInsertRows(index, i, i)
          keys.insert(i, key)
          self.endInsertRows()

      if isinstance(value, ObservableCollection):
        value.recap(functools.partial(self.__on_change, value))

    else:
      if is_column:
        j = bisect.bisect_left(self.columns, key)
        key_in_parent = source.key_in_parent()
        parent_keys = self.item_to_keys[id(source.parent)]
        k = bisect.bisect_left(parent_keys, key_in_parent)
        value_index = self.index(k, self.prefix_columns+j, index.parent())
        self.dataChanged.emit(value_index, value_index)
        return

      i = bisect.bisect_left(keys, key)
      self.beginRemoveRows(index, i, i)
      del keys[i]
      self.endRemoveRows()

      if isinstance(source, ObservableList):
        for k in range(key, len(source)):
          self.__on_change(source, ObservableCollection.Action.SET, k, source[k])
        if not recursed:
          self.__on_change(source, ObservableCollection.Action.DELETE, len(source), None, True)
        

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    print('data', index.row(), index.column(), role)
    item = self.index_to_item[index.parent().internalId()]
    keys = self.item_to_keys[id(item)]
    if index.column() >= self.prefix_columns:
      print('column')
      key = self.columns[index.column()-self.prefix_columns]
      row_key = keys[index.row()]
      print('data10', item, row_key)
      item = item.get(row_key, None)
      if not isinstance(item, ObservableCollection):
        return None

      value = item.get(key, None)
    else:
      print('key')
      key = keys[index.row()]
      value = item[key] if index.column() == 1 else key
      if isinstance(value, ObservableCollection):
        value = ''
    print(key, value)

    if role == Qt.ItemDataRole.DisplayRole:
      if isinstance(value, bool):
        return None
      return value
    elif role == Qt.ItemDataRole.EditRole:
      if isinstance(value, bool):
        return None
      return value
    elif role == Qt.ItemDataRole.CheckStateRole:
      if not isinstance(value, bool):
        return None
      return Qt.CheckState.Checked if value else Qt.CheckState.Unchecked

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    print('setData', index, value, role)
    item = self.index_to_item[index.parent().internalId()]
    keys = self.item_to_keys[id(item)]
    if index.column() >= self.prefix_columns:
      key = self.columns[index.column()-self.prefix_columns]
      row_key = keys[index.row()]
      item = item.get(row_key, None)
      if not isinstance(item, ObservableCollection):
        return False
    else:
      key = keys[index.row()]
    print(item,key)

    if role == Qt.ItemDataRole.DisplayRole:
      item[key] = value
      return True
    elif role == Qt.ItemDataRole.EditRole:
      item[key] = value
      return True
    elif role == Qt.ItemDataRole.CheckStateRole:
      item[key] = Qt.CheckState(value) == Qt.CheckState.Checked
      return True
    else:
      return super().setData(index, value, role)

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    print('flags1', index)
    flags = super().flags(index)

    item = self.index_to_item[index.parent().internalId()]
    keys = self.item_to_keys[id(item)]
    if index.column() >= self.prefix_columns:
      key = self.columns[index.column()-self.prefix_columns]
      row_key = keys[index.row()]
      print('flags10', item, key, row_key)
      item = item.get(row_key, None)
      if item is None:
        return flags
    else:
      key = keys[index.row()]
    if index.column() > 0 and self.is_editable(item, key):
      value = item[key] if key in item else None
      print('flags2', key, isinstance(value, bool))
      if isinstance(value, bool):
        flags |= Qt.ItemFlag.ItemIsUserCheckable
      else:
        flags |= Qt.ItemFlag.ItemIsEditable
    print('flags3', index.row(), index.column(), flags, Qt.ItemFlag.ItemIsEditable)
    return flags

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if self.show_extra_values:
        if section == 0:
          return self.key_column
        elif section == 1:
          return self.value_column
        else:
          return self.columns[section-self.prefix_columns]
      else:
        if section == 0:
          return self.key_column
        else:
          return self.columns[section-self.prefix_columns]

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    #print('index', row, column, parent)
    parent_id = 0
    if parent.isValid():
      parent_id = parent.internalId()
    key = parent_id, row, column
    if key not in self.index_cache:
      print('createIndex', row, column, self.next_index_id)
      new_index = self.createIndex(row, column, self.next_index_id)
      self.index_cache[key] = new_index
      self.index_to_parent[new_index.internalId()] = parent
      self.next_index_id += 1
    return self.index_cache[key]

  def get_location(self, index: QModelIndex) -> typing.Tuple[ObservableCollection, typing.Any]:
    item = self.index_to_item[index.parent().internalId()]
    keys = self.item_to_keys[id(item)]
    if index.column() >= self.prefix_columns:
      key = self.columns[index.column()-self.prefix_columns]
      row_key = keys[index.row()]
      item = item[row_key]
    else:
      key = keys[index.row()]
    return item, key

  
  def parent(self, index: QModelIndex) -> QModelIndex:
    print('parent', index)

    #print(self.index_to_item)
    #print(index.row(), index.column(), index.internalId())
    return self.index_to_parent[index.internalId()]

  def rowCount(self, parent: QModelIndex) -> int:
    print('rowCount', parent.isValid())
    if parent.internalId() not in self.index_to_item:
      return 0

    item = self.index_to_item[parent.internalId()]
    keys = self.item_to_keys[id(item)]
    result = len(keys)
    print('rowCount', parent.isValid(), result)
    return result

  def columnCount(self, _: QModelIndex) -> int:
    result = self.prefix_columns + len(self.columns)
    print('columnCount', _.isValid(), result)
    return result

class TreeObservableCollectionDelegate(QItemDelegate):
  def __init__(self, model: TreeObservableCollectionModel, precision = 3,
               choices: typing.Callable[[ObservableCollection, typing.Any], typing.Optional[typing.List[str]]] = lambda a, b: None):
    super().__init__()
    self.model = model
    self.precision = precision
    self.choices = choices

  def createEditor(self, parent: QWidget, option: QStyleOptionViewItem, index: QModelIndex) -> QWidget:
    print('createEditor')
    item, key = self.model.get_location(index)
    choices = self.choices(item, key)
    if choices is not None:
      result = QComboBox(parent)
      return result

    value = item[key] if key in item else None
    if isinstance(value, int):
      result = QSpinBox(parent)
      result.setRange(-1000000, 1000000)
      return result
    elif isinstance(value, float):
      result = QDoubleSpinBox(parent)
      result.setRange(-1000000, 1000000)
      result.setDecimals(self.precision)
      return result

    return super().createEditor(parent, option, index)

  def setEditorData(self, editor: QWidget, index: QModelIndex) -> None:
    item, key = self.model.get_location(index)
    value = item[key] if key in item else None

    choices = self.choices(item, key)
    if choices is not None:
      editor.addItems(choices)
      editor.setCurrentText(value)
      return

    if isinstance(editor, QSpinBox):
      editor.setValue(value)
      return
    elif isinstance(editor, QDoubleSpinBox):
      editor.setValue(value)
      return
    return super().setEditorData(editor, index)

  def setModelData(self, editor: QWidget, model: QAbstractItemModel, index: QModelIndex):
    item, key = self.model.get_location(index)
    if isinstance(editor, QSpinBox):
      item[key] = editor.value()
      return
    elif isinstance(editor, QDoubleSpinBox):
      item[key] = editor.value()
      return
    elif isinstance(editor, QComboBox):
      item[key] = editor.currentText()
      return
    return super().setModelData(editor, model, index)

