import typing
import functools

from ..config import *
from ..qt import *
from ..task_controller.util import create_task_with_exc_handling
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc

class Delegate(QItemDelegate):
  def createEditor(self, parent: QWidget, option: QStyleOptionViewItem, index: QModelIndex) -> QWidget:
    if index.column() == 1:
      return QLineEdit(parent)
    return super().createEditor(parent, option, index)
    
  def setEditorData(self, editor: QWidget, index: QModelIndex) -> None:
    if index.column() == 1:
      edit = typing.cast(QLineEdit, editor)
      data = index.data()
      if isinstance(data, int):
        data = bin(index.data())
      edit.setText(data)
    return super().setEditorData(editor, index)
    
  def setModelData(self, editor: QWidget, model: QAbstractItemModel, index: QModelIndex):
    edit = typing.cast(QLineEdit, editor)
    if index.column() == 0:
      mask = int(edit.text(), 0)
    else:
      mask = edit.text()
    model.setData(index, mask)
    return super().setEditorData(editor, index)

class PosesModel(QAbstractItemModel):
  def __init__(self, config: ObservableDict):
    super().__init__()
    self.config = config
    self.config.add_observer(self.on_change, functools.partial(isdeleted, self))
    for k, v in enumerate(self.config):
      self.on_change(ObservableCollection.Action.SET, k, v)

  def on_change(self, action, key, value):
    if action == ObservableCollection.Action.SET:
      self.beginInsertRows(QModelIndex(), key, key)
      self.endInsertRows()

      value.add_observer(lambda *args: self.on_pose_change(key, *args), functools.partial(isdeleted, self))
      for k, v in enumerate(value):
        self.on_pose_change(key, ObservableCollection.Action.SET, k, v)
    else:
      self.beginRemoveRows(QModelIndex(), key, key)
      self.endRemoveRows()

  def on_pose_change(self, i, action, key, value):
    self.dataChanged.emit(self.index(i, key, QModelIndex()), self.index(i, key, QModelIndex()))


  def data(self, index: QModelIndex, role: int) -> typing.Any:
    #print('data', index.row(), index.column(), role)
    if not index.isValid():
      return None

    if role != Qt.ItemDataRole.DisplayRole:
      return None

    if index.row() >= len(self.config):
      return None

    temp = self.config[index.row()][index.column()]
    if index.column() == 0:
      temp = bin(temp)
    return temp

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)

    self.config[index.row()][index.column()] = value
    return True

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    #print('flags', index)
    flags = super().flags(index)
    if not index.isValid():
      return flags

    return flags | Qt.ItemFlag.ItemIsEditable

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Mask"
      elif section == 1:
        return "Name"
    return None

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    #print('index', row, column, parent, self.hasIndex(row, column, parent))
    if not self.hasIndex(row, column, parent):
      return QModelIndex()

    return self.createIndex(row, column, None)
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    #print('parent', index)
    return QModelIndex()

  def rowCount(self, parent: QModelIndex) -> int:
    if parent == QModelIndex():
      return len(self.config)
    return 0

  def columnCount(self, _: QModelIndex) -> int:
    #print('columnCount', _)
    return 2

class XsensEditorWidget(QWidget):
  def __init__(self, config: ObservableDict, stub):
    super().__init__()
    self.config = config
    self.stub = stub

    if 'Poses' not in self.config:
      config['Poses'] = []
    if 'Pose Hand' not in self.config:
      config['Pose Hand'] = 'Right'
    if 'Xsens Address' not in self.config:
      config['Xsens Address'] = '127.0.0.1:6004'
    if 'Send Type' not in self.config:
      config['Send Type'] = 'Current'

    qlist = QTreeView()
    model = PosesModel(config['Poses'])
    qlist.setModel(model)
    qlist.setItemDelegate(Delegate())

    layout = QVBoxLayout()
    combo = QComboBox()
    reset_button = QPushButton('Reset')
    cache_button = QPushButton('Cache')

    def reset():
      request = thalamus_pb2.NodeRequest(
        node = self.config['name'],
        json = '"Reset"'
      )
      create_task_with_exc_handling(stub.node_request(request))
    def cache():
      request = thalamus_pb2.NodeRequest(
        node = self.config['name'],
        json = '"Cache"'
      )
      create_task_with_exc_handling(stub.node_request(request))
    reset_button.clicked.connect(reset);
    cache_button.clicked.connect(cache);

    add_pose_button = QPushButton('Add Pose')
    remove_pose_button = QPushButton('Remove Pose')

    add_pose_button.clicked.connect(lambda: config['Poses'].append([32, '']))

    def remove_pose():
      for index in qlist.selectedIndexes():
        del self.config['Poses'][index.row()]
        break

    remove_pose_button.clicked.connect(remove_pose)

    address_row = QHBoxLayout()
    address_row.addWidget(QLabel('Xsens Address:'))
    self.address_edit = QLineEdit()
    self.address_edit.editingFinished.connect(lambda: config.update({'Xsens Address': self.address_edit.text()}))
    address_row.addWidget(self.address_edit)

    hand_row = QHBoxLayout()
    hand_row.addWidget(QLabel('Hand:'))
    self.hand_combo = QComboBox()
    self.hand_combo.addItem('Left')
    self.hand_combo.addItem('Right')
    self.hand_combo.currentTextChanged.connect(lambda t: config.update({'Pose Hand': t}))
    hand_row.addWidget(self.hand_combo)

    def set_send(checked, value):
      if checked:
        config['Send Type'] = value

    self.send_current_checkbox = QRadioButton('Send Current')
    self.send_current_checkbox.toggled.connect(lambda t: set_send(t, 'Current'))
    self.send_max_checkbox = QRadioButton('Send Max')
    self.send_max_checkbox.toggled.connect(lambda t: set_send(t, 'Max'))
    self.send_min_checkbox = QRadioButton('Send Min')
    self.send_min_checkbox.toggled.connect(lambda t: set_send(t, 'Min'))

    layout.addLayout(address_row)
    layout.addWidget(reset_button)
    layout.addWidget(cache_button)
    layout.addWidget(self.send_current_checkbox)
    layout.addWidget(self.send_max_checkbox)
    layout.addWidget(self.send_min_checkbox)
    layout.addLayout(hand_row)
    layout.addWidget(qlist)
    layout.addWidget(add_pose_button)
    layout.addWidget(remove_pose_button)

    self.setLayout(layout)

    self.config.add_observer(self.on_change, lambda: isdeleted(self))
    for k, v in self.config.items():
      self.on_change(None, k, v)

  def on_change(self, a, k, v):
    if k == 'Xsens Address':
      self.address_edit.setText(v)
    elif k == 'Pose Hand':
      self.hand_combo.setCurrentText(v)
    elif k == 'Send Type':
      if v == 'Current':
        self.send_current_checkbox.setChecked(True)
      elif v == 'Max':
        self.send_max_checkbox.setChecked(True)
      elif v == 'Min':
        self.send_min_checkbox.setChecked(True)
    elif k == 'Xsens Address Good':
      if v:
        self.address_edit.setStyleSheet('color: black')
      else:
        self.address_edit.setStyleSheet('color: red')
