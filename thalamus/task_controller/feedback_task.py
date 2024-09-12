import time
import json
import math
import enum
import struct
import typing
import threading
import queue as thread_queue
import random
import logging
import datetime
import asyncio
import functools
import subprocess

from .. import native_exe

from ..config import *
import pathlib
import grpc
from .. import util_pb2
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc

from .widgets import Form
from .util import (
  TaskContextProtocol, UdpProtocol, CanvasPainterProtocol, movella_decorator, TaskResult, MovellaReceiver,
  nidaq_decorator, NidaqmxTaskWrapper, create_task_with_exc_handling, animate)
from ..util import IterableQueue

from ..qt import *

LOGGER = logging.getLogger(__name__)

class ItemModel(QAbstractItemModel):
  def __init__(self, config: ObservableDict):
    super().__init__()
    self.config = config
    self.config.add_observer(self.on_rows_change, functools.partial(isdeleted, self))
    for k, v in enumerate(self.config):
      self.on_rows_change(ObservableCollection.Action.SET, k, v)

  def get_row(self, value):
    for k, v in enumerate(self.config):
      if v is value:
        return k
    assert False

  def on_rows_change(self, action, key, value):
    if action == ObservableCollection.Action.SET:
      self.beginInsertRows(QModelIndex(), key, key)
      self.endInsertRows()
      value.add_observer(lambda *args: self.on_row_change(value, *args), functools.partial(isdeleted, self))
      for k, v in value.items():
        self.on_row_change(value, ObservableCollection.Action.SET, k, v)
    else:
      self.beginRemoveRows(QModelIndex(), key, key)
      self.endRemoveRows()

  def on_row_change(self, row, action, key, value):
    i = self.get_row(row)
    self.dataChanged.emit(self.index(i, 0, QModelIndex()), self.index(i, 4, QModelIndex()))

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    #print('data', index.row(), index.column(), role)
    if not index.isValid():
      return None

    if role != Qt.ItemDataRole.DisplayRole:
      return None

    data = self.config[index.row()]
    if index.column() == 0:
      return data['Start']
    elif index.column() == 1:
      return data['End']

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)

    data = self.config[index.row()]
    if index.column() == 0:
      data['Start'] = value
      return True
    elif index.column() == 1:
      data['End'] = value
      return True
    return False

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    return super().flags(index) | Qt.ItemFlag.ItemIsEditable

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Start"
      elif section == 1:
        return "End"
    return None

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    if not self.hasIndex(row, column, parent):
      return QModelIndex()
    return self.createIndex(row, column, None)
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    return QModelIndex()

  def rowCount(self, parent: QModelIndex) -> int:
    return len(self.config) if not parent.isValid() else 0

  def columnCount(self, parent: QModelIndex) -> int:
    return 3

def create_widget(task_config: ObservableCollection) -> QWidget:
  """
  Returns a QWidget that will be used to edit the task configuration
  """
  result = QWidget()
  layout = QVBoxLayout()
  result.setLayout(layout)

  if 'Transitions' not in task_config:
    task_config['Transitions'] = []
  transitions = task_config['Transitions']

  if 'error' not in task_config:
    task_config['error'] = ''

  qlist = QTreeView()
  model = ItemModel(transitions)
  qlist.setModel(model)

  error_label = QLabel(task_config['error'])
  error_label.setStyleSheet('color: red')
  add_button = QPushButton('Add')
  remove_button = QPushButton('Remove')

  add_button.clicked.connect(lambda: transitions.append({'Start': '', 'End': ''}))

  def on_remove():
    row_set = set(item.row() for item in qlist.selectedIndexes())
    row_list = sorted(row_set, reverse=True)
    for row in row_list:
      print('on_remove', row)
      del transitions[row]
  remove_button.clicked.connect(on_remove)

  layout.addWidget(error_label)
  layout.addWidget(qlist)
  layout.addWidget(add_button)
  layout.addWidget(remove_button)

  def on_change(action, key, value):
    print('on_change', action, key, value)
    if key == 'error':
      print('set error')
      error_label.setText(value)

  task_config.add_observer(on_change, lambda: isdeleted(result))

  return result

channel = None
stub = None
queue = None
analog_queue = None
log_call = None
LAST_IMAGE = None
current_pose = None
xsens_task = None

@functools.lru_cache(maxsize=None)
def load_image(path: pathlib.Path):
  return QImage(str(path))

last_range = None
images = []
time_node = ''
analog_task = None
current_time = 0.0
last_video_file = ''
loaded_videos = {}
feedback_node = ''
feedback_channel = ''
accumulator = 0

class State(enum.Enum):
  HOLD = enum.auto()
  GO = enum.auto()

async def analog_processor(stream):
  global current_time
  try:
    async for message in stream:
      print(message)
      for span in message.spans:
        if span.name == 'ACCUMULATOR':
          accumulator += sum(message.data[span.begin:span.end])
  finally:
    stream.cancel()

@animate(60)
async def run(context: TaskContextProtocol) -> TaskResult:
  global channel, feedback_node, feedback_channel, stub, queue, analog_queue, log_call, LAST_IMAGE, current_pose, xsens_task, last_range, images, time_node, analog_task, last_video_file, accumulator

  if channel is None:
    channel = context.get_channel('localhost:50050')
    stub = thalamus_pb2_grpc.ThalamusStub(channel)

  task_config = context.task_config

  if analog_task is None:
    request = thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(name='Precision'))
    stream = stub.analog(request)
    analog_task = create_task_with_exc_handling(analog_processor(stream))

  transitions = task_config['Transitions']

  config_json = json.dumps(context.task_config.unwrap())
  await context.log(config_json)


  def renderer(painter):
    font = painter.font()
    font.setPointSize(150)
    painter.setFont(font)
    text_height = 200

    if state == State.HOLD:
      painter.fillRect(0, 0, context.widget.width(), context.widget.height(), QColor(255, 255, 0))
      if context.widget.height() <= text_height:
        return
      scale = min((context.widget.width())/start_image.width(), (context.widget.height() - text_height)/start_image.height())
      scaled_width = int(start_image.width()*scale)
      scaled_height = int(start_image.height()*scale)
      painter.drawImage(QRect((context.widget.width() - scaled_width)//2, 0, scaled_width, scaled_height), start_image)
      painter.drawText(QRect(0, context.widget.height()-text_height, context.widget.width(), text_height), Qt.AlignmentFlag.AlignCenter, 'HOLD')
    if state == State.GO:
      color = QColor(255, 0, 0) if start_filename == end_filename else QColor(0, 255, 0)
      painter.fillRect(0, 0, context.widget.width(), context.widget.height(), color)

      text = 'HOLD' if start_filename == end_filename else 'GO'

      scale = min((context.widget.width())/end_image.width(), (context.widget.height() - text_height)/end_image.height())
      scaled_width = int(end_image.width()*scale)
      scaled_height = int(end_image.height()*scale)
      painter.drawImage(QRect((context.widget.width() - scaled_width)//2, 0, scaled_width, scaled_height), end_image)
      painter.drawText(QRect(0, context.widget.height()-text_height, context.widget.width(), text_height), Qt.AlignmentFlag.AlignCenter, text)

  context.widget.renderer = renderer

  space_pressed = False
  space_future: typing.Optional[asyncio.Future] = None
  def on_key_release(e: QKeyEvent):
    if space_future is not None and not space_future.done() and e.key() == Qt.Key.Key_Space:
      space_future.set_result(None)

  context.widget.key_release_handler = on_key_release

  future = None
  for i, transition in enumerate(transitions):
    start_filename, end_filename = transition['Start'], transition['End']

    start_image = load_image(start_filename)
    if start_image.isNull():
      LOGGER.warn(f'failed to load {start_filename}')

    end_image = load_image(end_filename)
    if start_image.isNull():
      LOGGER.warn(f'failed to load {end_filename}')

    state = State.HOLD
    space_future = asyncio.get_event_loop().create_future()
    await space_future

    state = State.GO
    space_future = asyncio.get_event_loop().create_future()
    accumulator = 0
    await space_future

  return TaskResult(True)
