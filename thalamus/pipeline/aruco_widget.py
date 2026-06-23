from ..qt import *
from ..config import *
import functools
#import cv2.aruco
import pathlib
import traceback
import numpy
import logging

LOGGER = logging.getLogger(__name__)

# Default calibration-wand geometry (T-shaped, 3 markers, 4x4 30mm), measured
# from the wand STL.  Origin at the T-junction, units meters.
WAND_MARKERS = [
  {'id': 1, 'x': -0.040, 'y':  0.000000, 'z': 0.0, 'rx': 0.0, 'ry': 0.0, 'rz': 0.0, 'size': 0.030},
  {'id': 2, 'x':  0.040, 'y':  0.000000, 'z': 0.0, 'rx': 0.0, 'ry': 0.0, 'rz': 0.0, 'size': 0.030},
  {'id': 3, 'x':  0.000, 'y': -0.091652, 'z': 0.0, 'rx': 0.0, 'ry': 0.0, 'rz': 0.0, 'size': 0.030},
]

MARKER_KEYS = ['id', 'x', 'y', 'z', 'rx', 'ry', 'rz', 'size']
TRANSFORM_KEYS = [None, 'translation_x', 'translation_y', 'translation_z',
                  'rotation_x', 'rotation_y', 'rotation_z', None]


class BoardsModel(QAbstractItemModel):
  def __init__(self, config: ObservableList):
    super().__init__()
    self.building = False
    self.config = config
    self.config.add_observer(self.on_boards_change, functools.partial(isdeleted, self))
    for i, v in enumerate(self.config):
      self.on_boards_change(ObservableCollection.Action.SET, i, v)

  def is_layout(self, board):
    return 'Type' in board and board['Type'] == 'layout'

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
      # Recap the board's fields to wire observers.  Children rows are reported
      # by rowCount() once the board row is inserted, so suppress structural
      # marker inserts during this initial pass.
      self.building = True
      for k, v in board.items():
        self.on_board_change(board, ObservableCollection.Action.SET, k, v)
      self.building = False
    else:
      self.beginRemoveRows(QModelIndex(), key, key)
      self.endRemoveRows()

  def fill_ids(self, board):
    board_row = self.get_row(board)
    parent = self.index(board_row, 0, QModelIndex())
    LOGGER.debug('fill')

    new_size = board['Rows']*board['Columns']
    ids = board['ids']
    next_id = max(ids)+1 if ids else 0
    if new_size < len(ids):
      self.beginRemoveRows(parent, new_size+2, len(ids)-1+2)
      for i in range(len(ids)-1, new_size-1, -1):
        del ids[i]
      self.endRemoveRows()
    elif new_size > len(ids):
      self.beginInsertRows(parent, len(ids)+2, new_size-1+2)
      for i in range(len(ids), new_size):
        ids.append(next_id+i)
      self.endInsertRows()
    LOGGER.debug('filled')

  def on_marker_change(self, board, marker, action, key, value):
    markers = board['Markers']
    mi = None
    for i, m in enumerate(markers):
      if m is marker:
        mi = i
        break
    if mi is None or key not in MARKER_KEYS:
      return
    parent = self.index(self.get_row(board), 0, QModelIndex())
    index = self.index(mi+3, MARKER_KEYS.index(key), parent)
    self.dataChanged.emit(index, index)

  def on_markers_change(self, board, action, key, value):
    parent = self.index(self.get_row(board), 0, QModelIndex())
    if action == ObservableCollection.Action.SET:
      if not self.building:
        self.beginInsertRows(parent, key+3, key+3)
      value.add_observer(lambda *args: self.on_marker_change(board, value, *args),
                         functools.partial(isdeleted, self))
      if not self.building:
        self.endInsertRows()
    else:
      self.beginRemoveRows(parent, key+3, key+3)
      self.endRemoveRows()

  def on_board_change(self, board, action, key, value):
    i = self.get_row(board)
    LOGGER.debug('on_board_change %s %s %s %s', i, action, key, value)
    if key == 'Type':
      index = self.index(i, 0, QModelIndex())
      self.dataChanged.emit(index, index)
    elif key == 'Name':
      index = self.index(i, 0, QModelIndex())
      self.dataChanged.emit(index, index)
    elif key == 'Quality Check':
      index = self.index(i, 1, QModelIndex())
      self.dataChanged.emit(index, index)
    elif key == 'Markers':
      # Observe the Markers *list* itself (regular observers only fire when the
      # change originates on the observed collection - see config.__notify).
      value.add_observer(lambda *args: self.on_markers_change(board, *args), functools.partial(isdeleted, self))
      for k, v in enumerate(value):
        self.on_markers_change(board, ObservableCollection.Action.SET, k, v)
    elif key == 'Rows':
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
    elif key == 'translation_x':
      parent = self.index(i, 0, QModelIndex())
      index = self.index(1, 1, parent)
      self.dataChanged.emit(index, index)
    elif key == 'translation_y':
      parent = self.index(i, 0, QModelIndex())
      index = self.index(1, 2, parent)
      self.dataChanged.emit(index, index)
    elif key == 'translation_z':
      parent = self.index(i, 0, QModelIndex())
      index = self.index(1, 3, parent)
      self.dataChanged.emit(index, index)
    elif key == 'rotation_x':
      parent = self.index(i, 0, QModelIndex())
      index = self.index(1, 4, parent)
      self.dataChanged.emit(index, index)
    elif key == 'rotation_y':
      parent = self.index(i, 0, QModelIndex())
      index = self.index(1, 5, parent)
      self.dataChanged.emit(index, index)
    elif key == 'rotation_z':
      parent = self.index(i, 0, QModelIndex())
      index = self.index(1, 6, parent)
      self.dataChanged.emit(index, index)
    elif key == 'ids':
      parent = self.index(i, 0, QModelIndex())
      board.add_observer(lambda *args: self.on_ids_change(board, *args), functools.partial(isdeleted, self))
      for k, v in enumerate(value):
        self.on_ids_change(board, ObservableCollection.Action.SET, k, v)

  def on_ids_change(self, board, action, key, value):
    LOGGER.debug('on_ids_change %s %s %s', action, key, value)
    if action == ObservableCollection.Action.SET:
      i = self.get_row(board)
      parent = self.index(i, 0, QModelIndex())
      index = self.index(key+2, 1, parent)
      LOGGER.debug('on_ids_change %s %s %s %s', self.rowCount(parent), key+2, index.row(), index.column())
      self.dataChanged.emit(index, index)

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    if not index.isValid():
      return None

    if index.parent() == QModelIndex():
      board = self.config[index.row()]
      if self.is_layout(board):
        if role == Qt.ItemDataRole.CheckStateRole and index.column() == 1:
          return Qt.CheckState.Checked if board['Quality Check'] else Qt.CheckState.Unchecked
        if role not in (Qt.ItemDataRole.DisplayRole, Qt.ItemDataRole.EditRole):
          return None
        if index.column() == 0:
          return board['Name']
        elif index.column() == 1:
          return 'Quality Check'
        return None

      if role != Qt.ItemDataRole.DisplayRole:
        return None
      if index.column() == 0:
        return board['Rows']
      elif index.column() == 1:
        return board['Columns']
      elif index.column() == 2:
        return board['Marker Size']
      elif index.column() == 3:
        return board['Marker Separation']
      return None

    board = self.config[index.parent().row()]
    if self.is_layout(board):
      if role not in (Qt.ItemDataRole.DisplayRole, Qt.ItemDataRole.EditRole):
        return None
      r, c = index.row(), index.column()
      if r == 0:
        labels = ['', 'Tranlsation X:', 'Tranlsation Y:', 'Tranlsation Z:',
                  'Rotation X:', 'Rotation Y:', 'Rotation Z:', '']
        return labels[c] if c < len(labels) else None
      elif r == 1:
        if c == 0:
          return 'End Effector Transform:'
        key = TRANSFORM_KEYS[c] if c < len(TRANSFORM_KEYS) else None
        return board[key] if key else None
      elif r == 2:
        return MARKER_KEYS[c] if c < len(MARKER_KEYS) else None
      else:
        markers = board['Markers']
        mi = r - 3
        if mi >= len(markers) or c >= len(MARKER_KEYS):
          return None
        return markers[mi][MARKER_KEYS[c]]

    if role != Qt.ItemDataRole.DisplayRole:
      return None
    if index.column() == 0 and index.row() == 1:
      return 'End Effector Transform:'
    elif index.column() == 1 and index.row() == 0:
      return 'Tranlsation X:'
    elif index.column() == 2 and index.row() == 0:
      return 'Tranlsation Y:'
    elif index.column() == 3 and index.row() == 0:
      return 'Tranlsation Z:'
    elif index.column() == 4 and index.row() == 0:
      return 'Rotation X:'
    elif index.column() == 5 and index.row() == 0:
      return 'Rotation Y:'
    elif index.column() == 6 and index.row() == 0:
      return 'Rotation Z:'
    elif index.column() == 1 and index.row() == 1:
      return board['translation_x']
    elif index.column() == 2 and index.row() == 1:
      return board['translation_y']
    elif index.column() == 3 and index.row() == 1:
      return board['translation_z']
    elif index.column() == 4 and index.row() == 1:
      return board['rotation_x']
    elif index.column() == 5 and index.row() == 1:
      return board['rotation_y']
    elif index.column() == 6 and index.row() == 1:
      return board['rotation_z']
    elif index.column() == 0 and index.row() == 2:
      return 'IDs:'
    elif index.column() == 1 and index.row() >= 2:
      ids = board['ids']
      LOGGER.debug('data %s %s', ids, index.row())
      return ids[index.row()-2]

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    if index.parent() == QModelIndex():
      board = self.config[index.row()]
      if self.is_layout(board):
        if role == Qt.ItemDataRole.CheckStateRole and index.column() == 1:
          board['Quality Check'] = int(value) == int(Qt.CheckState.Checked)
          return True
        if role != Qt.ItemDataRole.EditRole:
          return super().setData(index, value, role)
        if index.column() == 0:
          board['Name'] = str(value)
          return True
        return False

      if role != Qt.ItemDataRole.EditRole:
        return super().setData(index, value, role)
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

    board = self.config[index.parent().row()]
    if self.is_layout(board):
      if role != Qt.ItemDataRole.EditRole:
        return super().setData(index, value, role)
      r, c = index.row(), index.column()
      if r == 1:
        key = TRANSFORM_KEYS[c] if c < len(TRANSFORM_KEYS) else None
        if not key:
          return False
        try:
          board[key] = float(value)
        except ValueError:
          return False
        return True
      elif r >= 3:
        markers = board['Markers']
        mi = r - 3
        if mi >= len(markers) or c >= len(MARKER_KEYS):
          return False
        marker = markers[mi]
        try:
          if c == 0:
            marker['id'] = int(value)
          else:
            marker[MARKER_KEYS[c]] = float(value)
        except ValueError:
          return False
        return True
      return False

    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)
    if index.row() == 1:
      if index.column() == 0:
        return False
      try:
        if index.column() == 1:
          board['translation_x'] = float(value)
        elif index.column() == 2:
          board['translation_y'] = float(value)
        elif index.column() == 3:
          board['translation_z'] = float(value)
        elif index.column() == 4:
          board['rotation_x'] = float(value)
        elif index.column() == 5:
          board['rotation_y'] = float(value)
        elif index.column() == 6:
          board['rotation_z'] = float(value)
      except ValueError:
        return False
    elif index.column() == 1 and index.row() >= 2:
      try:
        board['ids'][index.row()-2] = int(value)
      except ValueError:
        return False
    return True

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    if not index.isValid():
      return super().flags(index)

    base = super().flags(index)
    if index.parent() == QModelIndex():
      board = self.config[index.row()]
      if self.is_layout(board):
        if index.column() == 0:
          return base | Qt.ItemFlag.ItemIsEditable
        if index.column() == 1:
          return base | Qt.ItemFlag.ItemIsUserCheckable
        return base
      # Grid top-level: preserve original editability behavior.
      if index.row() == 0 or index.column() > 3:
        return base
      return base | Qt.ItemFlag.ItemIsEditable

    board = self.config[index.parent().row()]
    if self.is_layout(board):
      r, c = index.row(), index.column()
      if r == 1 and 1 <= c <= 6:
        return base | Qt.ItemFlag.ItemIsEditable
      if r >= 3 and 0 <= c < len(MARKER_KEYS):
        return base | Qt.ItemFlag.ItemIsEditable
      return base

    if index.column() == 0 or index.row() == 0:
      return base
    return base | Qt.ItemFlag.ItemIsEditable

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
      if self.is_layout(board):
        child_count = len(board['Markers']) + 3
      else:
        child_count = len(board['ids']) + 2
      result = self.createIndex(row, column, board) if row < child_count else QModelIndex()
      return result

  def parent(self, index: QModelIndex) -> QModelIndex:
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
        if self.is_layout(board):
          return len(board['Markers']) + 3
        ids = board['ids']
        LOGGER.debug('rowCount %s %s', ids, len(ids) + 2)
        return len(ids) + 2
      return 0

  def columnCount(self, _: QModelIndex) -> int:
    return 8

class ArucoWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()

    self.config = config
    if 'Boards' not in config:
      config['Boards'] = []
    boards = config['Boards']

    for board in boards:
      for k in ('translation_x', 'translation_y', 'translation_z', 'rotation_x', 'rotation_y', 'rotation_z'):
        if k not in board:
          board[k] = 0.0
      if 'Type' in board and board['Type'] == 'layout':
        if 'Name' not in board:
          board['Name'] = ''
        if 'Quality Check' not in board:
          board['Quality Check'] = False
        if 'Markers' not in board:
          board['Markers'] = []

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
    add_layout_button = QPushButton('Add Layout Board')
    add_marker_button = QPushButton('Add Marker')
    remove_marker_button = QPushButton('Remove Marker')
    remove_button = QPushButton('Remove')
    save_button = QPushButton('Generate Board')

    layout = QGridLayout()
    layout.addWidget(QLabel('Dictionary:'), 0, 0)
    layout.addWidget(dict_combo, 0, 1)
    layout.addWidget(qlist, 1, 0, 1, 2)
    layout.addWidget(add_button, 2, 0)
    layout.addWidget(remove_button, 2, 1)
    layout.addWidget(add_layout_button, 3, 0)
    layout.addWidget(add_marker_button, 3, 1)
    layout.addWidget(remove_marker_button, 4, 0)
    layout.addWidget(save_button, 4, 1)

    def max_marker_id():
      max_id = 0
      for board in boards:
        if 'ids' in board:
          max_id = max([max_id] + list(board['ids']))
        if 'Markers' in board:
          for marker in board['Markers']:
            max_id = max(max_id, marker['id'])
      return max_id

    def on_add():
      max_id = max_marker_id()
      board = {
        'Rows': 3,
        'Columns': 4,
        'Marker Size': .05,
        'Marker Separation': .01,
        'ids': list(range(max_id+1, max_id+1+12)),
        'translation_x': 0.0,
        'translation_y': 0.0,
        'translation_z': 0.0,
        'rotation_x': 0.0,
        'rotation_y': 0.0,
        'rotation_z': 0.0
      }
      boards.append(board)
    add_button.clicked.connect(on_add)

    def on_add_layout():
      board = {
        'Type': 'layout',
        'Name': 'wand',
        'Quality Check': True,
        'Markers': [dict(m) for m in WAND_MARKERS],
        'translation_x': 0.0,
        'translation_y': 0.0,
        'translation_z': 0.0,
        'rotation_x': 0.0,
        'rotation_y': 0.0,
        'rotation_z': 0.0
      }
      boards.append(board)
    add_layout_button.clicked.connect(on_add_layout)

    def selected_layout_board():
      for item in qlist.selectedIndexes():
        idx = item
        while idx.parent().isValid():
          idx = idx.parent()
        board = boards[idx.row()]
        if 'Type' in board and board['Type'] == 'layout':
          return board
      return None

    def on_add_marker():
      board = selected_layout_board()
      if board is None:
        return
      board['Markers'].append({
        'id': max_marker_id()+1, 'x': 0.0, 'y': 0.0, 'z': 0.0,
        'rx': 0.0, 'ry': 0.0, 'rz': 0.0, 'size': 0.030
      })
    add_marker_button.clicked.connect(on_add_marker)

    def on_remove_marker():
      for item in qlist.selectedIndexes():
        if not item.parent().isValid():
          continue
        board = boards[item.parent().row()]
        if 'Type' in board and board['Type'] == 'layout' and item.row() >= 3:
          mi = item.row() - 3
          if 0 <= mi < len(board['Markers']):
            del board['Markers'][mi]
            return
    remove_marker_button.clicked.connect(on_remove_marker)

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
        if 'Type' in board and board['Type'] == 'layout':
          continue
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
