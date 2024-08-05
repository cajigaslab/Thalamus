from ..qt import *
from ..config import *
import functools
import cv2.aruco
import pathlib
import traceback
import numpy

class BoardsModel(QAbstractItemModel):
  def __init__(self, config: ObservableList):
    super().__init__()
    self.config = config
    self.config.add_observer(self.on_boards_change, functools.partial(isdeleted, self))
    for i, v in enumerate(self.config):
      self.on_boards_change(ObservableCollection.Action.SET, i, v)

  def get_row(self, board):
    for i, v in enumerate(self.config):
      if board is v:
        return i
    assert False, 'Failed to find row for board'

  def on_boards_change(self, action, key, board):
    if action == ObservableCollection.Action.SET:
      self.beginInsertRows(QModelIndex(), key, key)
      self.endInsertRows()
      board.add_observer(lambda *args: self.on_board_change(board, *args), functools.partial(isdeleted, self))
      for k, v in enumerate(board):
        self.on_board_change(board, ObservableCollection.Action.SET, k, v)
    else:
      self.beginRemoveRows(QModelIndex(), key, key)
      self.endRemoveRows()

  def fill_ids(self, board):
    board_row = self.get_row(board)
    parent = self.index(board_row, 0, QModelIndex())

    new_size = board['Rows']*board['Columns']
    ids = board['ids']
    next_id = max(ids)+1 if ids else 0
    if new_size < len(ids):
      self.beginRemoveRows(parent, new_size, len(ids)-1)
      for i in range(len(ids)-1, new_size-1, -1):
        del ids[i]
      self.endRemoveRows()
    elif new_size > len(ids):
      self.beginInsertRows(parent, len(ids), new_size-1)
      for i in range(len(ids), new_size):
        ids.append(next_id+i)
      self.endInsertRows()

  def on_board_change(self, board, action, key, value):
    i = self.get_row(board)
    if key == 'Rows':
      index = self.index(i, 0, QModelIndex())
      self.dataChanged.emit(index, index)
      self.fill_ids(board)
    elif key == 'Columns':
      index = self.index(i, 1, QModelIndex())
      self.dataChanged.emit(index, index)
      self.fill_ids(board)
    elif key == 'Marker Size':
      index = self.index(i, 2, QModelIndex())
      self.dataChanged.emit(index, index)
    elif key == 'Marker Separation':
      index = self.index(i, 3, QModelIndex())
      self.dataChanged.emit(index, index)
    elif key == 'ids':
      parent = self.index(i, 0, QModelIndex())
      board.add_observer(lambda *args: self.on_ids_change(board, *args), functools.partial(isdeleted, self))
      for k, v in enumerate(value):
        self.on_ids_change(board, ObservableCollection.Action.SET, k, v)

  def on_ids_change(self, board, action, key, value):
    if action == ObservableCollection.Action.SET:
      i = self.get_row(board)
      parent = self.index(i, 0, QModelIndex())
      index = self.index(key, 0, parent)
      self.dataChanged.emit(index, index)

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    #print('data', index.row(), index.column(), role)
    if not index.isValid():
      return None

    if role != Qt.ItemDataRole.DisplayRole:
      return None

    if index.parent() == QModelIndex():
      board = self.config[index.row()]
      if index.column() == 0:
        return board['Rows']
      elif index.column() == 1:
        return board['Columns']
      elif index.column() == 2:
        return board['Marker Size']
      elif index.column() == 3:
        return board['Marker Separation']
    elif index.column() == 0 and index.row() == 0:
      return 'IDs:'
    elif index.column() == 1:
      board_row = index.parent().row()
      board = self.config[board_row]
      ids = board['ids']
      return ids[index.row()]

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)

    if index.parent() == QModelIndex():
      board = self.config[index.row()]
      if index.column() == 0:
        try:
          board['Rows'] = int(value)
        except ValueError:
          return False
      elif index.column() == 1:
        try:
          board['Columns'] = int(value)
        except ValueError:
          return False
      elif index.column() == 2:
        try:
          board['Marker Size'] = float(value)
        except ValueError:
          return False
      elif index.column() == 3:
        try:
          board['Marker Separation'] = float(value)
        except ValueError:
          return False
      else:
        return False
      return True
    else:
      board_row = index.parent().row()
      board = self.config[board_row]
      try:
        board['ids'][index.row()] = int(value)
      except ValueError:
        return False
      return True

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    if index.parent().isValid() and index.column() == 0:
      return super().flags(index)
    return super().flags(index) | Qt.ItemFlag.ItemIsEditable

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Rows"
      elif section == 1:
        return "Columns"
      elif section == 2:
        return "Marker Size (m)"
      elif section == 3:
        return "Marker Separation (m)"
    return None

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    if parent == QModelIndex():
      result = self.createIndex(row, column, None) if row < len(self.config) else QModelIndex()
      return result
    else:
      board = self.config[parent.row()]
      ids = board['ids']
      result = self.createIndex(row, column, board) if row < len(ids) else QModelIndex()
      return result
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    #print('parent', index)
    board = index.internalPointer()
    if board is None:
      return QModelIndex()
    i = self.get_row(board)
    return self.createIndex(i, 0, None)

  def rowCount(self, parent: QModelIndex) -> int:
    if not parent.isValid():
      return len(self.config)
    else:
      board = parent.internalPointer()
      if board is None:
        board = self.config[parent.row()]
        ids = board['ids']
        return len(ids)
      return 0

  def columnCount(self, _: QModelIndex) -> int:
    #print('columnCount', _)
    return 4

class ArucoWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()

    self.config = config
    if 'Boards' not in config:
      config['Boards'] = []
    boards = config['Boards']

    qlist = QTreeView()
    model = BoardsModel(boards)
    qlist.setModel(model)

    dict_combo = QComboBox()
    dict_combo.addItems([
      "DICT_4X4_50",
      "DICT_4X4_100",
      "DICT_4X4_250",
      "DICT_4X4_1000",
      "DICT_5X5_50",
      "DICT_5X5_100",
      "DICT_5X5_250",
      "DICT_5X5_1000",
      "DICT_6X6_50",
      "DICT_6X6_100",
      "DICT_6X6_250",
      "DICT_6X6_1000",
      "DICT_7X7_50",
      "DICT_7X7_100",
      "DICT_7X7_250",
      "DICT_7X7_1000",
      "DICT_ARUCO_ORIGINAL",
      "DICT_APRILTAG_16h5",
      "DICT_APRILTAG_25h9",
      "DICT_APRILTAG_36h10",
      "DICT_APRILTAG_36h11",
      "DICT_ARUCO_MIP_36h12"
    ])
    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')
    save_button = QPushButton('Generate Board')

    layout = QGridLayout()
    layout.addWidget(QLabel('Dictionary:'), 0, 0)
    layout.addWidget(dict_combo, 0, 1)
    layout.addWidget(qlist, 1, 0, 1, 2)
    layout.addWidget(add_button, 2, 0)
    layout.addWidget(remove_button, 2, 1)
    layout.addWidget(save_button, 3, 0, 1, 2)

    def on_add():
      max_id = 0
      print(boards)
      for board in boards:
        max_id = max(board['ids'] + [max_id])
      board = {
        'Rows': 3,
        'Columns': 4,
        'Marker Size': .05,
        'Marker Separation': .01,
        'ids': list(range(max_id+1, max_id+1+12))
      }
      boards.append(board)
    add_button.clicked.connect(on_add)

    dict_combo.currentTextChanged.connect(lambda text: config.update({'Dictionary': text}))
    self.dict_combo = dict_combo

    def on_remove():
      rows = set()
      for item in qlist.selectedIndexes():
        if item.parent().isValid():
          item = item.parent()
        rows.add(item.row())
      for row in sorted(rows, reverse=True):
        del boards[row]
    remove_button.clicked.connect(on_remove)

    def on_generate():
      dict_id_str = config['Dictionary']
      dict_id = getattr(cv2.aruco, dict_id_str)

      aruco_dict = cv2.aruco.getPredefinedDictionary(dict_id)

      rows = set()
      for item in qlist.selectedIndexes():
        if item.parent().isValid():
          item = item.parent()
        rows.add(item.row())

      for row in sorted(rows, reverse=True):
        board = boards[row]
        file_name = QFileDialog.getSaveFileName(self, "Save file", str(pathlib.Path.cwd() / f'board{row}.png'), "*.png *.jpg")
        if file_name:
          rows, columns = board['Rows'], board['Columns']
          ids = numpy.array(board['ids'])
          grid = cv2.aruco.GridBoard((columns, rows), board['Marker Size'], board['Marker Separation'], aruco_dict, ids)
          image = grid.generateImage((100*columns, 100*rows))
          cv2.imwrite(file_name[0], image)

    save_button.clicked.connect(on_generate)

    self.setLayout(layout)

    config.add_observer(self.on_change, functools.partial(isdeleted, self))
    for k, v in config.items():
      self.on_change(None, k, v)

  def on_change(self, action, key, value):
    if key == 'Dictionary':
      self.dict_combo.setCurrentText(value)
