from ..qt import *
import typing
import sys
import enum
import math
import time
import json
import shelve
import logging
import pathlib
import datetime
import itertools
import collections
import pkg_resources
from ..config import *
import functools
import h5py
import asyncio
from ..task_controller.util import create_task_with_exc_handling
from ..util import IterableQueue

from ..util import MeteredUpdater
from .oculomatic_widget import OculomaticWidget
from .distortion_widget import DistortionWidget
from .normalize_widget import NormalizeWidget
from .genicam_widget import GenicamWidget
from .channel_picker_widget import ChannelPickerWidget
from .ros2_widget import Ros2Widget
from .algebra_widget import AlgebraWidget
from .channel_viewer import ChannelViewerWidget
from .xsens_widget import XsensEditorWidget
from .lua_widget import LuaWidget
from .log_widget import LogWidget
from .wave_widget import WaveWidget
from .touchscreen_widget import TouchScreenWidget
from .intan_widget import IntanWidget
from .spikeglx_widget import SpikeGlxWidget
from .aruco_widget import ArucoWidget
from .hexascope_widget import HexascopeWidget
from .sync_widget import SyncWidget
from .stim_widget import StimWidget
from .analog_widget import AnalogWidget
from .storage_widget import StorageWidget
from .run2_widget import Run2Widget
from ..util import NodeSelector
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from .data_widget import DataWidget
import OpenGL.GL
import grpc
from .. import process

from ..observable_item_models import TreeObservableCollectionModel

LOGGER = logging.getLogger(__name__)

DOCK_AREAS = {
 'right': Qt.DockWidgetArea.RightDockWidgetArea,
 'left': Qt.DockWidgetArea.LeftDockWidgetArea,
 'top': Qt.DockWidgetArea.TopDockWidgetArea,
 'bottom': Qt.DockWidgetArea.BottomDockWidgetArea,
 '': Qt.DockWidgetArea.NoDockWidgetArea
}

INVERSE_DOCK_AREAS = {v[1]: v[0] for v in DOCK_AREAS.items()}

class UserDataType(enum.Enum):
  DEFAULT = enum.auto()
  COMBO_BOX = enum.auto()
  CHECK_BOX = enum.auto()
  SPINBOX = enum.auto()
  DOUBLE_SPINBOX = enum.auto()

class UserData(typing.NamedTuple):
  type: UserDataType
  key: str
  value: typing.Any
  options: typing.List[str]

class Factory(typing.NamedTuple):
  create_widget: typing.Optional[typing.Callable[[ObservableDict, thalamus_pb2_grpc.ThalamusStub], QWidget]]
  fields: typing.List[UserData]

def create_test_widget(node):
  label = node['type']

  widget = QLabel()
  widget.setText(label)
  return widget

def create_run_widget(node: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
  return NodeSelector(node, 'Targets')

class AlphaOmegaTableModel(QAbstractTableModel):
  def __init__(self, channels: typing.List[ObservableDict]):
    super().__init__()
    self.channels = channels

  def rowCount(self, parent: QModelIndex = ...) -> int:
    return len(self.channels)

  def columnCount(self, parent: QModelIndex = ...) -> int:
    return 4

  def data(self, index: QModelIndex, role: int = ...) -> typing.Any:
    channel = self.channels[index.row()]
    if role == Qt.ItemDataRole.DisplayRole:
      if index.column() == 0:
        return channel['name']
      elif index.column() == 1:
        return channel['id']
      elif index.column() == 2:
        return channel['frequency']
    elif role == Qt.ItemDataRole.EditRole:
      if index.column() == 0:
        return channel['name']
      elif index.column() == 1:
        return channel['id']
      elif index.column() == 2:
        return channel['frequency']
      elif index.column() == 3:
        return channel['selected']
    if role == Qt.ItemDataRole.CheckStateRole:
      if index.column() == 3:
        return Qt.CheckState.Checked if channel['selected'] else Qt.CheckState.Unchecked

  def setData(self, index: QModelIndex, value: typing.Any, role: int = ...) -> bool:
    print('setData', index, value, role)
    channel = self.channels[index.row()]
    channel['selected'] = value == Qt.CheckState.Checked
    self.dataChanged.emit(index, index)
    return True

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    if index.column() == 3:
      return Qt.ItemFlag.ItemIsUserCheckable | super().flags(index)
    return super().flags(index)

  def headerData(self, section: int, orientation: Qt.Orientation, role: int = ...) -> typing.Any:
    if role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return 'Name'
      elif section == 1:
        return 'ID'
      elif section == 2:
        return 'Frequency'
      elif section == 3:
        return 'Selected'

def create_alpha_omega_widget(node: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
  if 'all_channels' not in node:
    node['all_channels'] = {}

  all_channels = node['all_channels']

  #At some point I started showing these keys in the UI so I switched to capitalized keys.  The restore lab are the
  #only ones using Alpha Omega so I thought it would be easy to update their config files.  However, the lower case
  #names keep popping up, possibly copied and pasted from older configs.  It's been a disaster.
  #
  #The below code checks the capitalization in the config and uses lower case keys if they are present.

  if all_channels:
    first_channel = next(iter(all_channels.values()))
    is_upper = 'Name' in first_channel
  else:
    is_upper = True

  if is_upper:
    key_column = 'ID'
    columns = ['Name', 'Frequency', 'Selected']
    selected = 'Selected'
  else:
    key_column = 'id'
    columns = ['name', 'frequency', 'selected']
    selected = 'selected'


  result = QWidget()
  layout = QVBoxLayout()
  refresh_button = QPushButton('Refresh')
  tree = QTableView()
  tree.verticalHeader().hide()
  model = TreeObservableCollectionModel(all_channels, key_column=key_column, columns=columns, show_extra_values=False, is_editable=lambda c, k: k == selected)
  sort_model = QSortFilterProxyModel()
  sort_model.setSourceModel(model)
  tree.setModel(sort_model)
  tree.setSortingEnabled(True)
  layout.addWidget(refresh_button)
  layout.addWidget(tree)
  result.setLayout(layout)

  def on_refresh():
    request = thalamus_pb2.NodeRequest(
      node = node['name'],
      json = '\"load_channels\"'
    )

    create_task_with_exc_handling(stub.node_request(request))

  refresh_button.clicked.connect(on_refresh);

  return result

FACTORIES = {
  'NONE': Factory(None, []),
  'STIM_PRINTER': Factory(None, []),
  'NIDAQ': Factory(None, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'Sample Rate', 1000.0, []),
    UserData(UserDataType.SPINBOX, 'Poll Interval', 16, []),
    UserData(UserDataType.CHECK_BOX, 'Zero Latency', False, []),
    UserData(UserDataType.DEFAULT, 'Channel', 'Dev1/ai0', []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'NIDAQ_OUT': Factory(StimWidget, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.CHECK_BOX, 'Fast Foward', False, []),
    UserData(UserDataType.DEFAULT, 'Channel', 'Dev1/ao0', []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'ALPHA_OMEGA': Factory(create_alpha_omega_widget, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DEFAULT, 'MAC Address', '', []),
    UserData(UserDataType.SPINBOX, 'Polling Interval', 16, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'TOGGLE': Factory(None, [
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.SPINBOX, 'Channel',  0, []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'Threshold', '1.6', []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'XSENS': Factory(XsensEditorWidget, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.SPINBOX, 'Port', 9763, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'HAND_ENGINE': Factory(None, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DEFAULT, 'Address', "localhost:9000", []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'Amplitude', 5.0, []),
    UserData(UserDataType.SPINBOX, 'Duration (ms)', 16, []),
  ]),
  'INTAN': Factory(IntanWidget, [
    UserData(UserDataType.CHECK_BOX, 'Connected', False, []),
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DEFAULT, 'Address', "localhost", []),
    UserData(UserDataType.SPINBOX, 'Command Port', 5000, []),
    UserData(UserDataType.SPINBOX, 'Waveform Port', 5001, []),
  ]),
  'SPIKEGLX': Factory(SpikeGlxWidget, [
    UserData(UserDataType.CHECK_BOX, 'Connected', False, []),
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.CHECK_BOX, 'Stream', False, []),
    UserData(UserDataType.SPINBOX, 'Poll Interval (ms)', 10, []),
    UserData(UserDataType.DEFAULT, 'Address', "localhost:4142", []),
  ]),
  'PULSE': Factory(None, [
    UserData(UserDataType.CHECK_BOX, 'Toggle', False, []),
    UserData(UserDataType.CHECK_BOX, 'Generate Level', False, []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'Amplitude', 5.0, []),
    UserData(UserDataType.SPINBOX, 'Duration (ms)', 100, []),
  ]),
  'WAVE': Factory(WaveWidget, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'Sample Rate', 1000.0, []),
    UserData(UserDataType.SPINBOX, 'Poll Interval', 16, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, [])
  ]),
  'STORAGE': Factory(StorageWidget, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DEFAULT, 'Sources', '', []),
    UserData(UserDataType.DEFAULT, 'Output File', 'test.tha', []),
    UserData(UserDataType.CHECK_BOX, 'Compress Analog', False, []),
    UserData(UserDataType.CHECK_BOX, 'Compress Video', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, [])
  ]),
  'STARTER': Factory(None, [
    UserData(UserDataType.SPINBOX, 'Channel',  0, []),
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'Threshold', 1.6, []),
    UserData(UserDataType.DEFAULT, 'Targets', '', []),
  ]),
  'RUNNER': Factory(create_run_widget, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DEFAULT, 'Targets', '', []),
  ]),
  'RUNNER2': Factory(Run2Widget, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
  ]),
  'OPHANIM': Factory(None, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DEFAULT, 'Address', '', []),
  ]),
  'TASK_CONTROLLER': Factory(lambda c, s: QWidget(), [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.DEFAULT, 'Address', 'localhost:50051', []),
  ]),
  'FFMPEG': Factory(None, [
    UserData(UserDataType.DEFAULT, 'Input Format', '', []),
    UserData(UserDataType.DEFAULT, 'Input Name', '', []),
    UserData(UserDataType.DEFAULT, 'Filter', '', []),
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
    #UserData(UserDataType.DEFAULT, 'Time Source', '', []),
  ]),
  'VIDEO': Factory(None, [
    UserData(UserDataType.DEFAULT, 'File Name', '', []),
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'ANALOG': Factory(AnalogWidget, [
    UserData(UserDataType.CHECK_BOX, 'Widget is Touchpad', False, [])
  ]),
  'OCULOMATIC': Factory(lambda c, s: OculomaticWidget(c, s) , [
    UserData(UserDataType.SPINBOX, 'Threshold', 100, []),
    UserData(UserDataType.SPINBOX, 'Min Area', 0, []),
    UserData(UserDataType.SPINBOX, 'Max Area', 100, []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'X Gain', 0.0, []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'Y Gain', 0.0, []),
    UserData(UserDataType.CHECK_BOX, 'Invert X', False, []),
    UserData(UserDataType.CHECK_BOX, 'Invert Y', False, []),
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.CHECK_BOX, 'Computing', False, []),
    UserData(UserDataType.CHECK_BOX, 'Render Thresholded', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'DISTORTION': Factory(lambda c, s: DistortionWidget(c, s), [
    UserData(UserDataType.SPINBOX, 'Threshold', 100, []),
    UserData(UserDataType.CHECK_BOX, 'Invert', False, []),
    UserData(UserDataType.SPINBOX, 'Rows', 8, []),
    UserData(UserDataType.SPINBOX, 'Columns', 8, []),
    UserData(UserDataType.CHECK_BOX, 'Collecting', False, []),
    UserData(UserDataType.CHECK_BOX, 'Computing', False, []),
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'THREAD_POOL': Factory(None, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'GENICAM': Factory(lambda c, s: GenicamWidget(c, s), [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'CHANNEL_PICKER': Factory(ChannelPickerWidget, []),
  'SYNC': Factory(SyncWidget, []),
  'NORMALIZE': Factory(lambda c, s: NormalizeWidget(c, s), [
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.SPINBOX, 'Min', 0.0, []),
    UserData(UserDataType.SPINBOX, 'Max', 1.0, []),
  ]),
  'ALGEBRA': Factory(lambda c, s: AlgebraWidget(c, s), [
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.DEFAULT, 'Equation', '', [])]),
  'LUA': Factory(lambda c, s: LuaWidget(c, s), [
    UserData(UserDataType.DEFAULT, 'Source', '', [])]),
  'TOUCH_SCREEN': Factory(TouchScreenWidget, [
    UserData(UserDataType.DEFAULT, 'Source', '', [])]),
  'REMOTE': Factory(None, [
    UserData(UserDataType.DEFAULT, 'Address', '', []),
    UserData(UserDataType.DEFAULT, 'Node', '', []),
    UserData(UserDataType.DOUBLE_SPINBOX, 'Probe Frequency', 10.0, []),
    UserData(UserDataType.SPINBOX, 'Probe Size', 128, []),
    UserData(UserDataType.CHECK_BOX, 'Running', False, [])]),
  'ROS2': Factory(lambda c, s: Ros2Widget(c, s), []),
  'PUPIL': Factory(None, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
  ]),
  'CHESSBOARD': Factory(None, [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
    UserData(UserDataType.SPINBOX, 'Height', 512, []),
    UserData(UserDataType.SPINBOX, 'Rows', 8, []),
    UserData(UserDataType.SPINBOX, 'Columns', 8, []),
  ]),
  'LOOP_TEST': Factory(None, [
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.DEFAULT, 'Channel', '', []),
  ]),
  'TEST_PULSE_NODE': Factory(None, [
    UserData(UserDataType.DEFAULT, 'Input', '', []),
    UserData(UserDataType.DEFAULT, 'Output', '', []),
  ]),
  'LOG': Factory(lambda c, s: LogWidget(c, s), []),
  'ARUCO': Factory(lambda c, s: ArucoWidget(c, s), [
    UserData(UserDataType.CHECK_BOX, 'Running', False, []),
    UserData(UserDataType.CHECK_BOX, 'Marker Mode', False, []),
    UserData(UserDataType.CHECK_BOX, 'View', False, []),
    UserData(UserDataType.DEFAULT, 'Source', '', []),
    UserData(UserDataType.COMBO_BOX, 'Dictionary',  "DICT_4X4_50", [
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
      "DICT_ARUCO_MIP_36h12"])
  ]),
  'HEXASCOPE': Factory(HexascopeWidget, []),
}

FACTORY_NAMES = {}

class Delegate(QItemDelegate):
  def createEditor(self, parent: QWidget, option: QStyleOptionViewItem, index: QModelIndex) -> QWidget:
    user_data = typing.cast(UserData, index.data(Qt.ItemDataRole.UserRole))
    if index.column() == 0 and index.parent() == QModelIndex():
      return super().createEditor(parent, option, index)
    elif user_data.type == UserDataType.COMBO_BOX:
      return QComboBox(parent)
    elif user_data.type == UserDataType.CHECK_BOX:
      return QCheckBox(parent)
    elif user_data.type == UserDataType.SPINBOX:
      result = QSpinBox(parent)
      result.setRange(-1000000, 1000000)
      return result
    elif user_data.type == UserDataType.DOUBLE_SPINBOX:
      result = QDoubleSpinBox(parent)
      result.setRange(-1000000.0, 1000000.0)
      return result
    else:
      return super().createEditor(parent, option, index)
    
  def setEditorData(self, editor: QWidget, index: QModelIndex) -> None:
    user_data = typing.cast(UserData, index.data(Qt.ItemDataRole.UserRole))
    if index.column() == 0 and index.parent() == QModelIndex():
      return super().setEditorData(editor, index)
    elif user_data.type == UserDataType.COMBO_BOX:
      combo_box = typing.cast(QComboBox, editor)
      for value in user_data.options:
        combo_box.addItem(FACTORY_NAMES[value] if index.column() == 0 else value, value)
      for i, value in enumerate(user_data.options):
        if value == index.data():
          combo_box.setCurrentIndex(i)
    elif user_data.type == UserDataType.CHECK_BOX:
      check_box = typing.cast(QCheckBox, editor)
      check_box.setChecked(user_data.value)
    elif user_data.type == UserDataType.SPINBOX:
      spin_box = typing.cast(QSpinBox, editor)
      spin_box.setValue(index.data())
    elif user_data.type == UserDataType.SPINBOX:
      double_spin_box = typing.cast(QDoubleSpinBox, editor)
      double_spin_box.setValue(index.data())
    else:
      return super().setEditorData(editor, index)
    
  def setModelData(self, editor: QWidget, model: QAbstractItemModel, index: QModelIndex):
    user_data = typing.cast(UserData, index.data(Qt.ItemDataRole.UserRole))
    if index.column() == 0 and index.parent() == QModelIndex():
      return super().setModelData(editor, model, index)
    if user_data.type == UserDataType.COMBO_BOX:
      combo_box = typing.cast(QComboBox, editor)
      text = combo_box.currentData()
      model.setData(index, text)
    elif user_data.type == UserDataType.CHECK_BOX:
      check_box = typing.cast(QCheckBox, editor)
      model.setData(index, check_box.isChecked())
    elif user_data.type == UserDataType.SPINBOX:
      spin_box = typing.cast(QSpinBox, editor)
      model.setData(index, spin_box.value())
    elif user_data.type == UserDataType.SPINBOX:
      double_spin_box = typing.cast(QDoubleSpinBox, editor)
      model.setData(index, double_spin_box.value())
    else:
      return super().setModelData(editor, model, index)
    
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg # type: ignore
from matplotlib.figure import Figure # type: ignore
import matplotlib.animation as animation
import numpy
import scipy.spatial.transform
import enum

def assert_gl():
  b = OpenGL.GL.glGetError()
  assert b == 0, f'OpenGL.GL.glGetError() == {b}'

class SegmentIndex(enum.Enum):
  Pelvis = 0
  L5 = 1
  L3 = 2
  T12 = 3
  T8 = 4
  Neck = 5
  Head = 6
  RightShoulder = 7
  RightUpperArm = 8
  RightForearm = 9
  RightHand = 10
  LeftShoulder = 11
  LeftUpperArm = 12
  LeftForearm = 13
  LeftHand = 14
  RightUpperLeg = 15
  RightLowerLeg = 16
  RightFoot = 17
  RightToe = 18
  LeftUpperLeg = 19
  LeftLowerLeg = 20
  LeftFoot = 21
  LeftToe = 22
  LeftCarpus = 23
  LeftFirstMC = 24
  LeftFirstPP = 25
  LeftFirstDP = 26
  LeftSecondMC = 27
  LeftSecondPP = 28
  LeftSecondMP = 29
  LeftSecondDP = 30
  LeftThirdMC = 31
  LeftThirdPP = 32
  LeftThirdMP = 33
  LeftThirdDP = 34
  LeftFourthMC = 35
  LeftFourthPP = 36
  LeftFourthMP = 37
  LeftFourthDP = 38
  LeftFifthMC = 39
  LeftFifthPP = 40
  LeftFifthMP = 41
  LeftFifthDP = 42
  RightCarpus = 43
  RightFirstMC = 44
  RightFirstPP = 45
  RightFirstDP = 46
  RightSecondMC = 47
  RightSecondPP = 48
  RightSecondMP = 49
  RightSecondDP = 50
  RightThirdMC = 51
  RightThirdPP = 52
  RightThirdMP = 53
  RightThirdDP = 54
  RightFourthMC = 55
  RightFourthPP = 56
  RightFourthMP = 57
  RightFourthDP = 58
  RightFifthMC = 59
  RightFifthPP = 60
  RightFifthMP = 61
  RightFifthDP = 62

def compute_normals(faces: numpy.ndarray):
  result = numpy.zeros_like(faces)
  for i in range(0, faces.shape[0], 3):
    one, two = faces[i+1] - faces[i], faces[i+2] - faces[i]
    normal = numpy.cross(one, two)
    normal /= numpy.linalg.norm(normal)
    result[i] = normal
    result[i+1] = normal
    result[i+2] = normal
  return result

def to_rod(origin: numpy.ndarray, destination: numpy.ndarray, radius: float, faces: int):
  points = numpy.zeros((faces*12, 3), dtype=numpy.float64)

  para = destination - origin
  para /= numpy.linalg.norm(para)

  perp = numpy.array([-(para[1] + para[2])/para[0], 1, 1])
  perp /= numpy.linalg.norm(perp)

  rotation = scipy.spatial.transform.Rotation.from_rotvec(para*2*numpy.pi/faces)

  corner = radius*perp
  for i in range(faces):
    next_corner = rotation.apply(corner)

    points[3*i] = origin
    points[3*i+1] = origin + corner
    points[3*i+2] = origin + next_corner

    corner = next_corner

  corner = radius*perp
  for i in range(faces):
    next_corner = rotation.apply(corner)

    points[3*faces + 3*i] = destination
    points[3*faces + 3*i+1] = destination + corner
    points[3*faces + 3*i+2] = destination + next_corner

    corner = next_corner

  corner = radius*perp
  for i in range(faces):
    next_corner = rotation.apply(corner)

    points[6*faces + 6*i] = origin + corner
    points[6*faces + 6*i+2] = origin + next_corner
    points[6*faces + 6*i+1] = destination + corner

    points[6*faces + 6*i+3] = destination + corner
    points[6*faces + 6*i+5] = origin + next_corner
    points[6*faces + 6*i+4] = destination + next_corner

    corner = next_corner
    
  return points

FACES = 6
RADIUS = .005

class XsensView(QOpenGLWidget):
  def __init__(self, vertices, config: ObservableCollection):
    super().__init__()
    self.vertices = vertices
    self.mv_matrix = QMatrix4x4()
    self.proj_matrix = QMatrix4x4()
    self.reset()
    self.last_pos: typing.Optional[QPoint] = None
    self.config = config

    body = numpy.array([
      0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
      7, 8, 8, 9, 9, 10,
      11, 12, 12, 13, 13, 14,
      15, 16, 16, 17, 17, 18,
      19, 20, 20, 21, 21, 22,
      5, 7,
      5, 11,
      0, 15,
      0, 19], dtype=numpy.uint16)
    hand = numpy.array([
      23, 24,
      24, 25, 25, 26,
      23, 27,
      27, 28, 28, 29, 29, 30,
      23, 31,
      31, 32, 32, 33, 33, 34,
      23, 35,
      35, 36, 36, 37, 37, 38,
      23, 39,
      39, 40, 40, 41, 41, 42], dtype=numpy.uint16)
    self.indices = numpy.hstack([body, hand, hand+20])

    #adapt to rendering triangles

    self.triangle_indices = numpy.arange(len(self.indices)/2*FACES*12, dtype=numpy.uint16)

  def mousePressEvent(self, a0: QMouseEvent) -> None:
    self.last_pos = a0.pos()

  def mouseReleaseEvent(self, a0: QMouseEvent) -> None:
    self.last_pos = None

  def mouseMoveEvent(self, a0: QMouseEvent) -> None:
    if self.last_pos is None:
      return
    current_pos = a0.pos()

    delta = current_pos - self.last_pos

    horizontal = QQuaternion.fromAxisAndAngle(self.up, -delta.x()/5)
    vertical = QQuaternion.fromAxisAndAngle(self.right, delta.y()/5)
    self.position = horizontal * vertical * self.position
    self.up = vertical * self.up
    self.right = horizontal  * self.right
    
    self.update()
    self.last_pos = current_pos

  def reset(self):
    self.position = QVector3D(0, 2, 0)
    self.up = QVector3D(0, 0, 1)
    self.right = QVector3D(1, 0, 0)

  def wheelEvent(self, a0: QWheelEvent) -> None:
    if a0.angleDelta().y() > 0:
      self.position *= .9
    else:
      self.position *= 1.1

  def resizeGL(self, width: int, height: int) -> None:
    self.proj_matrix.setToIdentity()
    self.proj_matrix.perspective(90, width/height, 1e-6, 1e6)

  def initializeGL(self) -> None:
    self.program = OpenGL.GL.glCreateProgram()
    self.vertex = OpenGL.GL.glCreateShader(OpenGL.GL.GL_VERTEX_SHADER)
    self.fragment = OpenGL.GL.glCreateShader(OpenGL.GL.GL_FRAGMENT_SHADER)
    self.vertex_code = pkg_resources.resource_string(__name__, 'shaders/xsens.vert')
    self.fragment_code = pkg_resources.resource_string(__name__, 'shaders/xsens.frag')

    OpenGL.GL.glEnable(OpenGL.GL.GL_DEPTH_TEST)

    OpenGL.GL.glShaderSource(self.vertex, self.vertex_code)
    OpenGL.GL.glShaderSource(self.fragment, self.fragment_code)
    # Compile shaders
    OpenGL.GL.glCompileShader(self.vertex)
    if not OpenGL.GL.glGetShaderiv(self.vertex, OpenGL.GL.GL_COMPILE_STATUS):
        error = OpenGL.GL.glGetShaderInfoLog(self.vertex).decode()
        LOGGER.error("Vertex shader compilation error: %s", error)
    OpenGL.GL.glCompileShader(self.fragment)
    if not OpenGL.GL.glGetShaderiv(self.fragment, OpenGL.GL.GL_COMPILE_STATUS):
        error = OpenGL.GL.glGetShaderInfoLog(self.fragment).decode()
        LOGGER.error(error)
        raise RuntimeError("Fragment shader compilation error")
    OpenGL.GL.glAttachShader(self.program, self.vertex)
    OpenGL.GL.glAttachShader(self.program, self.fragment)
    OpenGL.GL.glLinkProgram(self.program)
    if not OpenGL.GL.glGetProgramiv(self.program, OpenGL.GL.GL_LINK_STATUS):
      LOGGER.error(OpenGL.GL.glGetProgramInfoLog(self.program))
      raise RuntimeError('Linking error')
    self.vertex_buffer, self.normal_buffer, self.index_buffer = OpenGL.GL.glGenBuffers(3)
    self.vertex_array, self.normal_array, self.index_array = OpenGL.GL.glGenVertexArrays(3)
    self.proj_location = OpenGL.GL.glGetUniformLocation(self.program, 'proj_matrix')
    self.mv_location = OpenGL.GL.glGetUniformLocation(self.program, 'mv_matrix')
    self.normal_location = OpenGL.GL.glGetUniformLocation(self.program, 'normal_matrix')

  def paintGL(self):
    OpenGL.GL.glClearColor(0, 0, 0, 1)
    OpenGL.GL.glClear(OpenGL.GL.GL_COLOR_BUFFER_BIT| OpenGL.GL.GL_DEPTH_BUFFER_BIT)

    OpenGL.GL.glUseProgram(self.program)
    assert_gl()

    proj_data = numpy.array(self.proj_matrix.data(), dtype=numpy.float32)
    OpenGL.GL.glUniformMatrix4fv(self.proj_location, 1, False, proj_data)
    assert_gl()

    self.mv_matrix.setToIdentity()
    self.mv_matrix.lookAt(self.position, QVector3D(0, 0, 0), self.up)
    mv_data = numpy.array(self.mv_matrix.data(), dtype=numpy.float32)
    normal_data = numpy.array(self.mv_matrix.normalMatrix().data(), dtype=numpy.float32)
    OpenGL.GL.glUniformMatrix4fv(self.mv_location, 1, False, mv_data)
    OpenGL.GL.glUniformMatrix3fv(self.normal_location, 1, False, normal_data)
    assert_gl()

    position = self.vertices[:,:3]
    rods = []
    for i in range(0, len(self.indices), 2):
      from_index, to_index = self.indices[i], self.indices[i+1]
      origin, destination = position[from_index], position[to_index]
      rods.append(to_rod(origin, destination, RADIUS, FACES))
    position = numpy.nan_to_num(numpy.vstack(rods))
    normals = compute_normals(position).astype(numpy.float32)

    focused = (position - position[self.config['focus']]).astype(numpy.float32)

    OpenGL.GL.glViewport(0, 0, self.width(), self.height())
    assert_gl()
    OpenGL.GL.glBindBuffer(OpenGL.GL.GL_ARRAY_BUFFER, self.vertex_buffer)
    assert_gl()
    OpenGL.GL.glBufferData(OpenGL.GL.GL_ARRAY_BUFFER, focused.nbytes, focused, OpenGL.GL.GL_DYNAMIC_DRAW)
    assert_gl()
    OpenGL.GL.glBindBuffer(OpenGL.GL.GL_ARRAY_BUFFER, self.normal_buffer)
    assert_gl()
    OpenGL.GL.glBufferData(OpenGL.GL.GL_ARRAY_BUFFER, normals.nbytes, normals, OpenGL.GL.GL_DYNAMIC_DRAW)
    assert_gl()
    OpenGL.GL.glBindBuffer(OpenGL.GL.GL_ELEMENT_ARRAY_BUFFER, self.index_buffer)
    assert_gl()
    OpenGL.GL.glBufferData(OpenGL.GL.GL_ELEMENT_ARRAY_BUFFER, self.triangle_indices.nbytes, self.triangle_indices, OpenGL.GL.GL_DYNAMIC_DRAW)
    assert_gl()
    
    #OpenGL.GL.glBindVertexArray(self.vertex_array)
    #assert_gl()
    loc = OpenGL.GL.glGetAttribLocation(self.program, "vertex")
    assert_gl()
    OpenGL.GL.glBindBuffer(OpenGL.GL.GL_ARRAY_BUFFER, self.vertex_buffer)
    assert_gl()
    OpenGL.GL.glEnableVertexAttribArray(loc)
    assert_gl()
    OpenGL.GL.glVertexAttribPointer(loc, 3, OpenGL.GL.GL_FLOAT, False, 0, None)
    assert_gl()
    
    #OpenGL.GL.glBindVertexArray(self.normal_array)
    #assert_gl()
    loc = OpenGL.GL.glGetAttribLocation(self.program, "normal")
    assert_gl()
    OpenGL.GL.glBindBuffer(OpenGL.GL.GL_ARRAY_BUFFER, self.normal_buffer)
    assert_gl()
    OpenGL.GL.glEnableVertexAttribArray(loc)
    assert_gl()
    OpenGL.GL.glVertexAttribPointer(loc, 3, OpenGL.GL.GL_FLOAT, False, 0, None)
    assert_gl()

    OpenGL.GL.glBindBuffer(OpenGL.GL.GL_ELEMENT_ARRAY_BUFFER, self.index_buffer)
    assert_gl()

    OpenGL.GL.glDrawElements(OpenGL.GL.GL_TRIANGLES, self.triangle_indices.shape[0], OpenGL.GL.GL_UNSIGNED_SHORT, None)
    assert_gl()

class XsensWidget(QWidget):
  def __init__(self, node: ObservableDict, stream: typing.AsyncIterable[thalamus_pb2.XsensResponse]):
    self.node = node
    self.stream = stream

    if 'view_geometry' not in node:
      node['view_geometry'] = [100, 100, 400, 400]
    x, y, w, h = node['view_geometry']
    self.view_geometry_updater = MeteredUpdater(node['view_geometry'], datetime.timedelta(seconds=1), lambda: isdeleted(self))

    if 'focus' not in node:
      node['focus'] = SegmentIndex.L3.value

    super().__init__()

    self.segments = numpy.zeros((63, 7))

    self.view = XsensView(self.segments, node)
    layout = QGridLayout()
    layout.setColumnStretch(0, 0)
    layout.setColumnStretch(1, 1)

    layout.addWidget(QLabel('Focus:'), 0, 0)
    self.focus_combobox = QComboBox()
    for i in SegmentIndex:
      self.focus_combobox.addItem(i.name, i)
      if i.value == node['focus']:
        self.focus_combobox.setCurrentIndex(self.focus_combobox.count()-1)
        self.view.focus = i
    layout.addWidget(self.focus_combobox, 0, 1)

    def on_focus_change(i):
      data = self.focus_combobox.currentData()
      node['focus'] = data.value
    self.focus_combobox.currentIndexChanged.connect(on_focus_change)

    layout.addWidget(self.view, 1, 0, 1, 2)

    layout.addWidget(QLabel('Pose:'), 2, 0)
    self.pose_label = QLabel()
    layout.addWidget(self.pose_label, 2, 1)

    reset_button = QPushButton('Reset')
    reset_button.clicked.connect(self.view.reset)
    layout.addWidget(reset_button, 3, 0, 1, 2)

    self.setLayout(layout)

    self.setWindowTitle(node['name'])

    node.add_observer(self.on_change, functools.partial(isdeleted, self))

    self.move(x, y)
    self.resize(w, h)

    create_task_with_exc_handling(self.__stream_task(stream))
    
    self.show()

  async def __stream_task(self, stream: typing.AsyncIterable[thalamus_pb2.GraphResponse]):
    async for response in stream:
      self.pose_label.setText(response.pose_name)
      for segment in response.segments:
        self.segments[segment.id-1,:] = segment.x, segment.y, segment.z, segment.q0, segment.q1, segment.q2, segment.q3
      self.view.update()

  def on_change(self, action, key, value):
    if key == 'View':
      if not value:
        self.close()
    self.view.update()

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
    self.node['View'] = False
    self.stream.cancel()
    return super().closeEvent(a0)

QTKEY_TO_CODE = {
    Qt.Key.Key_0: 'Digit0',
    Qt.Key.Key_1: 'Digit1',
    Qt.Key.Key_2: 'Digit2',
    Qt.Key.Key_3: 'Digit3',
    Qt.Key.Key_4: 'Digit4',
    Qt.Key.Key_5: 'Digit5',
    Qt.Key.Key_6: 'Digit6',
    Qt.Key.Key_7: 'Digit7',
    Qt.Key.Key_8: 'Digit8',
    Qt.Key.Key_9: 'Digit9',
    Qt.Key.Key_A: 'KeyA',
    Qt.Key.Key_B: 'KeyB',
    Qt.Key.Key_C: 'KeyC',
    Qt.Key.Key_D: 'KeyD',
    Qt.Key.Key_E: 'KeyE',
    Qt.Key.Key_F: 'KeyF',
    Qt.Key.Key_G: 'KeyG',
    Qt.Key.Key_H: 'KeyH',
    Qt.Key.Key_I: 'KeyI',
    Qt.Key.Key_J: 'KeyJ',
    Qt.Key.Key_K: 'KeyK',
    Qt.Key.Key_L: 'KeyL',
    Qt.Key.Key_M: 'KeyM',
    Qt.Key.Key_N: 'KeyN',
    Qt.Key.Key_O: 'KeyO',
    Qt.Key.Key_P: 'KeyP',
    Qt.Key.Key_Q: 'KeyQ',
    Qt.Key.Key_R: 'KeyR',
    Qt.Key.Key_S: 'KeyS',
    Qt.Key.Key_T: 'KeyT',
    Qt.Key.Key_U: 'KeyU',
    Qt.Key.Key_V: 'KeyV',
    Qt.Key.Key_W: 'KeyW',
    Qt.Key.Key_X: 'KeyX',
    Qt.Key.Key_Y: 'KeyY',
    Qt.Key.Key_Z: 'KeyZ',
}

class GraphResponsePart:
  def __init__(self, original: thalamus_pb2.GraphResponse, i: int):
    self.original = original
    self.i = i

  @property
  def bins(self):
    return self.original.bins

  @property
  def spans(self):
    return self.original.spans[self.i:self.i+1]

class PlotStack(QWidget):
  def __init__(self, node: ObservableDict, stream: typing.AsyncIterable[thalamus_pb2.GraphResponse], bin_ns: int):
    super().__init__()
    self.bin_ns = bin_ns
    self.node = node
    self.stream = stream
    self.setWindowTitle(node['name'])

    if 'view_geometry' not in node:
      node['view_geometry'] = [100, 100, 400, 100]
    self.view_geometry_updater = MeteredUpdater(node['view_geometry'], datetime.timedelta(seconds=1), lambda: isdeleted(self))

    node.add_observer(self.on_change, functools.partial(isdeleted, self))

    x, y, w, h = node['view_geometry']
    self.move(x, y)
    self.resize(w, h)

    self.task = create_task_with_exc_handling(self.__stream_task(stream))

    self.__layout = QVBoxLayout()
    self.setLayout(self.__layout)
    
    self.show()
  
  def on_change(self, action, key, value):
    if key == 'View':
      if not value:
        self.close()

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
    self.node['View'] = False
    self.stream.cancel()
    self.task.cancel()
    return super().closeEvent(a0)

  async def __stream_task(self, stream: typing.AsyncIterable[thalamus_pb2.GraphResponse]):
    plots = []
    try:
      async for response in stream:
        for i, span in enumerate(response.spans):
          if len(plots) == i:
            new_plot = Plot(self.node, IterableQueue(), self.bin_ns)
            plots.append(new_plot)
            self.__layout.addWidget(new_plot)
          
          await plots[i].stream.put(GraphResponsePart(response, i))
    except asyncio.CancelledError:
      pass
    except grpc.aio.AioRpcError:
      pass

class Plot(QWidget):
  position = None

  def __init__(self, node: ObservableDict, stream: typing.AsyncIterable[thalamus_pb2.GraphResponse], bin_ns: int):
    super().__init__()

    self.name = ""
    self.bin_ns = bin_ns
    self.current_ns = 0
    self.ydata = []
    self.paths: typing.List[QPainterPath] = [QPainterPath(), QPainterPath()]
    self.offset_ns = 0
    self.duration_ns = 10e9
    self.range = math.inf, -math.inf
    self.node = node
    self.stream = stream
    self.position = []
    self.linspace = numpy.linspace(0, 10, 2*1920)
    self.setWindowTitle(node['name'])

    self.task = create_task_with_exc_handling(self.__stream_task(stream))

  def paintEvent(self, event):
    super().paintEvent(event)
    
    if self.paths[0].isEmpty() and self.paths[1].isEmpty():
      range_size = 1
    else:
      if not self.paths[0].isEmpty():
        bounds = self.paths[0].boundingRect()
        range_size = bounds.height()
        self.range = min(bounds.y(), self.range[0]), max(bounds.y() + bounds.height(), self.range[1])

      if not self.paths[1].isEmpty():
        bounds = self.paths[1].boundingRect()
        range_size = bounds.height()
        self.range = min(bounds.y(), self.range[0]), max(bounds.y() + bounds.height(), self.range[1])

    if range_size == 0:
      range_size = 1
    range = self.range[0] - range_size/10, self.range[1] + range_size/10

    painter = QPainter(self)
    metrics = QFontMetrics(painter.font())
    
    pen = painter.pen()
    pen.setCosmetic(True)
    vertical_scale = (self.height() - 2*metrics.height())/(range[1] - range[0])
    painter.translate(metrics.height(), self.height() + range[0]*vertical_scale - metrics.height())
    painter.scale((self.width() - 2*metrics.height())/self.duration_ns, -vertical_scale)
    offset = -self.current_ns
    #painter.save()
    #painter.setClipRect(QRectF(0, 0, self.width(), self.height()))
    for path, color in zip(self.paths, [Qt.GlobalColor.blue, Qt.GlobalColor.blue, Qt.GlobalColor.blue]):
      pen.setColor(color)
      painter.setPen(pen)
      painter.save()
      painter.translate(offset, 0)
      painter.drawPath(path)
      painter.restore()
      offset += self.duration_ns
    #painter.restore()

    pen.setColor(Qt.GlobalColor.black)
    painter.setPen(pen)
    painter.drawRect(QRectF(0, range[0], 10, range[1] - range[0]))

    painter.resetTransform()

    painter.drawText(0, metrics.height(), str(self.range[1]))
    painter.drawText(0, self.height(), str(self.range[0]))

    name_bounds = metrics.boundingRect(self.name)
    painter.drawText(self.width() - name_bounds.width(), self.height(), self.name)

  async def __stream_task(self, stream: typing.AsyncIterable[thalamus_pb2.GraphResponse]):
    async for response in stream:
      looped = False
      if not len(response.bins):
        continue
      for i, span in enumerate(response.spans):
        self.name = span.name
        for value in response.bins[span.begin:span.end]:
          if self.current_ns >= self.duration_ns:
            self.paths = [self.paths[1], QPainterPath()]
            self.current_ns = 0

          if self.paths[1].elementCount() == 0:
            self.paths[1].moveTo(self.current_ns, value)
          else:
            self.paths[1].lineTo(self.current_ns, value)
          self.current_ns += self.bin_ns/2

      self.update()

class ItemModel(QAbstractItemModel):
  def __init__(self, nodes: ObservableList, stub: thalamus_pb2_grpc.ThalamusStub, address: str):
    super().__init__()
    self.nodes = nodes
    self.stub = stub
    self.address = address
    self.plots = {}
    self.procs: typing.Dict[int, asyncio.subprocess.Process] = {}
    for node in self.nodes:
      if 'Running' in node:
        node['Running'] = False

    self.nodes.add_observer(self.on_nodes, functools.partial(isdeleted, self))
    self.num_nodes = len(nodes)
    self.node_types = []
    for i, node in enumerate(self.nodes):
      self.on_nodes(ObservableCollection.Action.SET, i, node)

  def close(self):
    for k, v in self.procs.items():
      try:
        v.kill()
      except ProcessLookupError:
        pass

  def get_node_type(self, node: ObservableDict):
    for t, n in zip(self.node_types, self.nodes):
      if n is node:
        return t
    raise RuntimeError('Node not found')
  
  def data(self, index: QModelIndex, role: int) -> typing.Any:
    if not index.isValid():
      return None

    if role == Qt.ItemDataRole.UserRole:
      collection = index.internalPointer()
      if collection is self.nodes:
        if index.column() == 1:
          options = sorted(FACTORIES.keys())
          return UserData(UserDataType.COMBO_BOX, "", "", options)
      else:
        node = index.internalPointer()
        type = self.get_node_type(node)
        factory = FACTORIES[type]
        fields = factory.fields
        if index.row() >= len(fields):
          return None
        field = fields[index.row()]
        return field
    elif role == Qt.ItemDataRole.CheckStateRole and index.column() == 1:
      collection = index.internalPointer()
      if collection is self.nodes:
        return None

      node = index.internalPointer()
      type = self.get_node_type(node)
      factory = FACTORIES[type]
      fields = factory.fields
      field = fields[index.row()]
      if field.type != UserDataType.CHECK_BOX:
        return None
      value = node[field.key]
      return Qt.CheckState.Checked if value else Qt.CheckState.Unchecked

    if role != Qt.ItemDataRole.DisplayRole:
      return None

    collection = index.internalPointer()
    if collection == self.nodes:
      dict = self.nodes[index.row()]
      if index.column() == 0:
        return dict["name"]
      else:
        return FACTORY_NAMES[dict["type"]]

    node = index.internalPointer()
    type = self.get_node_type(node)
    factory = FACTORIES[type]
    fields = factory.fields
    field = fields[index.row()]

    if index.column() == 0:
      return field.key
    else:
      if field.type == UserDataType.CHECK_BOX:
        return None
      value = node[field.key]
      return value

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    if role == Qt.ItemDataRole.CheckStateRole:
      value = Qt.CheckState(value) == Qt.CheckState.Checked
    elif role == Qt.ItemDataRole.EditRole:
      pass
    else:
      return super().setData(index, value, role)

    collection = index.internalPointer()
    if collection == self.nodes:
      node = self.nodes[index.row()]
      if index.column() == 0:
        node["name"] = value
        self.dataChanged.emit(index, index, [role])
      else:
        if node['type'] != value:
          node['type'] = value
          self.dataChanged.emit(index, index, [role])
      return True
    else:
      node = index.internalPointer()
      type = self.get_node_type(node)
      factory = FACTORIES[type]
      fields = factory.fields
      field = fields[index.row()]
      print(node, field, value)
      if node[field.key] != value:
        node[field.key] = value
        self.dataChanged.emit(index, index, [role])
      return True
  
  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    if not index.isValid():
      return Qt.ItemFlag.NoItemFlags

    flags = super().flags(index)

    collection = index.internalPointer()
    if collection == self.nodes:
      return Qt.ItemFlag.ItemIsEditable | flags

    if index.column() == 1:
      node = collection
      type = self.get_node_type(node)
      factory = FACTORIES[type]
      fields = factory.fields
      field = fields[index.row()]
      if field.type == UserDataType.CHECK_BOX:
        return Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsUserCheckable | flags
      return Qt.ItemFlag.ItemIsEditable | flags

    return flags

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Name"
      else:
        return "Type"
    return None
  
  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
      if not self.hasIndex(row, column, parent):
        return QModelIndex()

      if not parent.isValid():
        if row < len(self.nodes):
          return self.createIndex(row, column, self.nodes)
      else:
        node = self.nodes[parent.row()]
        type = self.get_node_type(node)
        factory = FACTORIES[type]
        fields = factory.fields
        if row < len(fields):
          return self.createIndex(row, column, node)
      return QModelIndex()
  
  def parent(self, index: QModelIndex) -> QModelIndex:
      if not index.isValid():
        return QModelIndex()

      collection = index.internalPointer()
      if collection is self.nodes:
        return QModelIndex()

      dict = index.internalPointer()
      
      for i, node in enumerate(self.nodes):
        if dict is node:
          return self.createIndex(i, 0, self.nodes)
        
      raise RuntimeError("Failed to find index of node")

  def rowCount(self, parent: QModelIndex) -> int:
    if not parent.isValid():
      return self.num_nodes
    else:
      collection = parent.internalPointer()
      if collection == self.nodes:
        node = self.nodes[parent.row()]
        type = self.get_node_type(node)
        factory = FACTORIES[type]
        fields = factory.fields
        return len(fields)
      else:
        return 0

  def columnCount(self, _: QModelIndex) -> int:
    return 2

  def on_nodes(self, action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
    if action == ObservableCollection.Action.SET:
      value.add_observer(functools.partial(self.on_node, value), functools.partial(isdeleted, self))

      self.node_types.insert(key, value['type'])

      self.beginInsertRows(QModelIndex(), key, key)
      self.num_nodes = len(self.nodes)
      self.endInsertRows()

      for key2, value2 in dict(value).items():
        self.on_node(value, ObservableCollection.Action.SET, key2, value2)
    elif action == ObservableCollection.Action.DELETE:
      del self.node_types[key]
      self.beginRemoveRows(QModelIndex(), key, key)
      self.num_nodes = len(self.nodes)
      self.endRemoveRows()

  def on_node(self, node: ObservableDict, action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
    if action == ObservableCollection.Action.SET:
      node_index = 0
      for i, n in enumerate(self.nodes):
        if node is n:
          node_index = i
          break
      

      if key == "name":
        index = self.createIndex(node_index, 0, self.nodes)
        self.dataChanged.emit(index, index)
      elif key == 'View':
        if value:
          async def create_widget():
            selector = thalamus_pb2.NodeSelector(name=node['name'])
            modalities = await self.stub.get_modalities(selector)
            if thalamus_pb2.Modalities.ImageModality in modalities.values:
              request = thalamus_pb2.NodeSelector(
                name = node["name"]
              )
              self.procs[id(node)] = await process.create_subprocess_exec(
                sys.executable, 
                "-m", "thalamus.image_viewer", '--address', self.address, '--node', node['name'])
            elif thalamus_pb2.Modalities.MocapModality in modalities.values:
              request = thalamus_pb2.NodeSelector(
                name = node["name"]
              )
              self.plots[id(node)] = XsensWidget(node, self.stub.xsens(request))
            elif thalamus_pb2.Modalities.AnalogModality in modalities.values:
              bin_ns = int(10e9/1920)
              request = thalamus_pb2.GraphRequest(
                node = thalamus_pb2.NodeSelector(name = node["name"]),
                bin_ns = bin_ns
              )
              self.plots[id(node)] = PlotStack(node, self.stub.graph(request), bin_ns)
          create_task_with_exc_handling(create_widget())
        else:
          if id(node) in self.procs:
            proc = self.procs[id(node)]
            del self.procs[id(node)]
            proc.kill()

        #  self.plots[id(node)].close()
      elif key == "type":
        front_index = self.createIndex(node_index, 0, self.nodes)
        index = self.createIndex(node_index, 1, self.nodes)


        old_factory = FACTORIES[self.node_types[node_index]]
        old_fields = old_factory.fields

        if old_fields:
          self.beginRemoveRows(index, 0, len(old_fields) - 1)
          self.node_types[node_index] = "NONE"
          self.endRemoveRows()

        factory = FACTORIES[value]
        fields = factory.fields

        if not fields:
          self.beginInsertRows(index, 0, len(fields) - 1)
          self.node_types[node_index] = value
          self.endInsertRows()
        else:
          self.node_types[node_index] = value

        for field in fields:
          if not field.key in node:
            node[field.key] = field.value
            self.dataChanged.emit(front_index, index)
      else:
        type = node['type']
        factory = FACTORIES[type]
        fields = factory.fields
        found = False
        for i, field in enumerate(fields):
          if field.key == key:
            found = True
            break
        if not found:
          return
        index = self.createIndex(i, 1, node)
        self.dataChanged.emit(index, index)

class PlaybackDialog(QDialog):
  def __init__(self, names: typing.List[str], parent=None):
    super().__init__(parent)
    self.names = names

  def exec(self):
    with shelve.open(pathlib.Path.home() / '.task_controller') as db:
      selected_cache = json.loads(db.get('playback_nodes', '[]'))

    layout = QVBoxLayout()
    widgets: typing.List[QCheckBox] = []
    for name in self.names:
      widget = QCheckBox(name)
      widget.setChecked(name in selected_cache)
      widgets.append(widget)
      layout.addWidget(widget)

    button_layout = QHBoxLayout()
    ok_button = QPushButton('Ok')
    ok_button.clicked.connect(self.reject)
    cancel_button = QPushButton('Cancel')
    cancel_button.clicked.connect(self.reject)
    button_layout.addWidget(ok_button)
    button_layout.addWidget(cancel_button)
    layout.addLayout(button_layout)

    self.setLayout(layout)
    super().exec()

    selected = [w.text() for w in widgets if w.isChecked()]
    with shelve.open(pathlib.Path.home() / '.task_controller') as db:
      db['playback_nodes'] = json.dumps(selected)

    return selected

class ThalamusWindow(QMainWindow):
  def __init__(self, address, state: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub, done_future: asyncio.Future):
    super().__init__()
    self.model: typing.Optional[ItemModel] = None
    self.state = state
    self.stub = stub
    self.address = address
    self.done_future = done_future
    self.config_menu_enabled = False
    self.filename = None
    self.grid_layout = QGridLayout()
    self.data_views = {}
    self.dock_widgets: typing.List[ThalamusDockWidget] = []
    self.channel_viewer: typing.Optional[QWidget] = None

  def enable_config_menu(self, filename: typing.Optional[str]):
    self.filename = filename
    self.config_menu_enabled = True

  def closeEvent(self, a0: QCloseEvent) -> None:
    for v in self.dock_widgets:
      v.close()
    if self.channel_viewer:
      self.channel_viewer.close()
    if self.model is not None:
      self.model.close()
    self.done_future.set_result(None)

  async def load(self):
    for key in list(FACTORIES.keys()):
      response = await self.stub.get_type_name(thalamus_pb2.StringMessage(value=key))
      if response.value:
        FACTORY_NAMES[key] = response.value
      else:
        del FACTORIES[key]

    if self.filename:
      self.setWindowTitle(f'Thalamus: {self.filename}')
    else:
      self.setWindowTitle(f'Thalamus')

    if 'thalamus_view_geometry' not in self.state:
      self.state['thalamus_view_geometry'] = [100, 100, 384, 768]
    x, y, w, h = self.state['thalamus_view_geometry']
    self.view_geometry_updater = MeteredUpdater(self.state['thalamus_view_geometry'], datetime.timedelta(seconds=1), lambda: isdeleted(self))
    self.move(x, y)
    self.resize(w, h)

    add_button = QPushButton('Add')
    add_button.clicked.connect(self.on_add)
    remove_button = QPushButton('Remove')
    remove_button.clicked.connect(self.on_remove)
    self.view = QTreeView()
    self.view.setItemDelegate(Delegate())

    self.model = ItemModel(self.state['nodes'], self.stub, self.address)
    self.view.setModel(self.model)
    self.model.dataChanged.connect(self.on_data_changed)

    self.view.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
    self.view.selectionModel().selectionChanged.connect(self.on_selection_changed)

    self.grid_layout.addWidget(self.view, 0, 0, 1, 2)
    self.grid_layout.addWidget(add_button, 1, 0)
    self.grid_layout.addWidget(remove_button, 1, 1)

    central_widget = QWidget()
    central_widget.setLayout(self.grid_layout)
    self.setCentralWidget(central_widget)

    menubar = self.menuBar()
    filemenu = menubar.addMenu('File')
    if self.config_menu_enabled:
      filemenu.addAction('Save Config').triggered.connect(self.on_save_config)
      filemenu.addAction('Save Config As').triggered.connect(self.on_save_as_config)
      filemenu.addAction('Load Config').triggered.connect(self.on_load_config)
    filemenu.addAction('Replay').triggered.connect(self.on_replay)

    if 'data_views' not in self.state:
      self.state['data_views'] = []
    if 'node_widgets' not in self.state:
      self.state['node_widgets'] = []

    def view_channel_info():
      self.channel_viewer = ChannelViewerWidget(self.state['nodes'], self.stub)
      self.channel_viewer.setGeometry(100, 100, 300, 600)
      self.channel_viewer.show()

    viewmenu = menubar.addMenu('View')
    viewmenu.addAction('Add Data View').triggered.connect(lambda: self.state['data_views'].append({}))
    viewmenu.addAction('View Channel Info').triggered.connect(view_channel_info)

    self.state['data_views'].add_observer(self.on_data_views_changed)
    for i, view in enumerate(self.state['data_views']):
      self.on_data_views_changed(ObservableCollection.Action.SET, i, view)

    self.state['node_widgets'].add_observer(self.on_node_widgets_changed)
    for i, widget in enumerate(self.state['node_widgets']):
      self.on_node_widgets_changed(ObservableCollection.Action.SET, i, widget)

  def moveEvent(self, a0: QMoveEvent) -> None:
    offset = self.frameGeometry().size() - self.geometry().size()
    position = a0.pos() - QPoint(offset.width(), offset.height())
    position = QPoint(max(0, position.x()), max(0, position.y()))
    self.view_geometry_updater[:2] = position.x(), position.y()
    return super().moveEvent(a0)

  def resizeEvent(self, a0: QResizeEvent) -> None:
    self.view_geometry_updater[2:] = a0.size().width(), a0.size().height()
    return super().resizeEvent(a0)

  def on_data_views_changed(self, action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
    if action == ObservableCollection.Action.SET:
      self.data_views[id(value)] = DataWidget(value, self.state, self.stub)
    else:
      del self.data_views[id(value)]

  def on_selection_changed(self, selected: QItemSelection, deselected: typing.Optional[QItemSelection]):
    print('on_selection_changed')
    indexes = selected.indexes()
    index = indexes[0] if indexes and indexes[0] else None
    if not index:
      return

    nodes = self.state['nodes']
    if index.parent() == QModelIndex():
      node = nodes[index.row()]
    else:
      node = nodes[index.parent().row()]
    print(node)

    node_type = node['type']
    factory = FACTORIES[node_type]
    if not factory.create_widget:
      return

    for i, widget in enumerate(self.state['node_widgets']):
      if widget['dock_area'] == 'right':
        del self.state['node_widgets'][i]
        break

    self.state['node_widgets'].append({
      'node': node['name'],
      'dock_area': 'right',
      'view_geometry': [-1, -1, -1, -1]
    })

  def on_node_widgets_changed(self, action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
    if action == ObservableCollection.Action.DELETE:
      w = self.dock_widgets[key]
      del self.dock_widgets[key]
      self.removeDockWidget(w)
      w.close()
      w.deleteLater()
    else:
      node_name = value['node']
      matches = [n for n in self.state['nodes'] if n['name'] == node_name]
      assert matches
      node = matches[0]

      node_type = node['type']
      dock = ThalamusDockWidget(value, node['name'], self)
      dock_is_deleted = lambda: isdeleted(dock)

      def name_observer(a, k, v):
        print('name_observer', k, v)
        if k == 'name':
          value['node'] = v
      node.add_observer(name_observer, dock_is_deleted)

      def title_observer(a, k, v):
        print('title_observer', k, v)
        if k == 'node':
          dock.setWindowTitle(v)
      value.add_observer(title_observer, dock_is_deleted)

      def type_observer(a, k, v):
        print('type_observer', k, v)
        if k == 'type':
          previous_widget = dock.widget();
          factory = FACTORIES[v]
          if factory.create_widget is None:
            dock.setWidget(QWidget())
          else:
            new_widget = factory.create_widget(node, self.stub)
            dock.setWidget(new_widget)
          if previous_widget is not None:
            previous_widget.close()
            previous_widget.deleteLater()
      node.add_observer(type_observer, dock_is_deleted)
      type_observer(None, 'type', node_type)
      
      dock_area = value['dock_area']
      x, y, w, h = value['view_geometry']
      if dock_area:
        self.addDockWidget(DOCK_AREAS[dock_area], dock)
        
        if w >= 0 and h >= 0:
          if dock_area in ('left', 'right'):
            length, orientation = w, Qt.Orientation.Horizontal
          else:
            length, orientation = h, Qt.Orientation.Vertical

          self.resizeDocks([dock], [length], orientation)
      else:
        self.addDockWidget(Qt.DockWidgetArea.TopDockWidgetArea, dock)
        dock.setFloating(True)
        if w >= 0 and h >= 0:
          dock.move(x, y)
          dock.resize(w, h)

      self.dock_widgets.append(dock)

  def on_data_changed(self, top_left: QModelIndex, bottom_right: QModelIndex, roles: typing.List[int]):
    print('on_data_changed')
    if top_left.parent() == QModelIndex():
      if top_left.column() == 1:
        self.on_selection_changed(QItemSelection(top_left, bottom_right), None)
      if self.model.rowCount(top_left) == 0:
        self.view.setExpanded(top_left, False)
        
  def on_save_config(self) -> None:
    """
    Save the current config to it's original file
    """
    if self.filename is None:
      self.on_save_as_config()
      return

    save(self.filename, self.state)

  def on_save_as_config(self) -> None:
    """
    Save the current config to a new file
    """
    filename = QFileDialog.getSaveFileName(self, "Save Config", "", "*.json")
    if filename and filename[0]:
      save(filename[0], self.state)
      self.filename = filename[0]
      self.setWindowTitle(f'Thalamus: {self.filename}')

  def on_load_config(self) -> None:
    """
    Load a config
    """
    filename = QFileDialog.getOpenFileName(self, "Load Config", "", "*.json")
    if filename and filename[0]:
      new_config = load(filename[0])
      self.state.merge(new_config)
      for node in new_config.get('nodes', []):
        if 'Running' in node:
          node['Running'] = False
      self.filename = filename[0]
      self.setWindowTitle(f'Thalamus: {self.filename}')

  def on_replay(self):
    file_name = QFileDialog.getOpenFileName(self, "Load Recording", "", "*.h5")
    if file_name and file_name[0]:
      all_nodes: typing.List[str] = []
      with h5py.File(file_name[0]) as h5file:
        analog_group = h5file['analog']
        xsens_group = h5file['xsens']
        all_nodes.extend(list(analog_group))
        if 'data' in xsens_group and isinstance(xsens_group['data'], h5py.Dataset):
          all_nodes.append('xsens')
        else:
          all_nodes.extend(list(xsens_group))

        dialog = PlaybackDialog(all_nodes, self)
        selected = dialog.exec()
        
      create_task_with_exc_handling(self.stub.replay(thalamus_pb2.ReplayRequest(filename=file_name[0], nodes=selected)))

  def on_add(self):
    nodes = self.state['nodes']
    names = set(node['name'] for node in nodes)

    node_number = 1
    while True:
      node_name = f'Node {node_number}'
      if node_name not in names:
        break
      node_number += 1

    new_node = {
      'name': node_name,
      'type': 'NONE'
    }
    nodes.append(new_node)

  def on_remove(self):
    indexes = self.view.selectedIndexes()
    if not indexes:
      return
    index = indexes[0]

    nodes = self.state['nodes']
    row = index.row() if index.parent() == QModelIndex() else index.parent().row()
    node = nodes[row]
    for i, widget in list(enumerate(self.state['node_widgets']))[::-1]:
      if widget['node'] == node['name']:
        del self.state['node_widgets'][i]

    if index.parent() == QModelIndex():
      del nodes[index.row()]
    else:
      del nodes[index.parent().row()]

class ThalamusDockWidget(QDockWidget):
  def __init__(self, config: ObservableDict, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.config = config
    self.view_geometry_updater = MeteredUpdater(config['view_geometry'], datetime.timedelta(seconds=1), lambda: isdeleted(self))

    def on_dock(area):
      text = INVERSE_DOCK_AREAS.get(area, '')
      self.config['dock_area'] = text

    def on_float(floating):
      if floating:
        on_dock(Qt.DockWidgetArea.NoDockWidgetArea)

    self.dockLocationChanged.connect(on_dock)
    self.topLevelChanged.connect(on_float)

  def moveEvent(self, a0: QMoveEvent) -> None:
    offset = self.frameGeometry().size() - self.geometry().size()
    position = a0.pos() - QPoint(offset.width(), offset.height())
    position = QPoint(max(0, position.x()), max(0, position.y()))
    self.view_geometry_updater[:2] = position.x(), position.y()
    return super().moveEvent(a0)

  def resizeEvent(self, a0: QResizeEvent) -> None:
    self.view_geometry_updater[2:] = a0.size().width(), a0.size().height()
    return super().resizeEvent(a0)

  def closeEvent(self, e):
    if self.config.parent is not None:
      for i, v in enumerate(self.config.parent):
        if v is self.config:
          del self.config.parent[i]

    widget = self.widget()
    if widget is not None:
      widget.closeEvent(e)
    super().closeEvent(e)
