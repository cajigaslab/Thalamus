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
import itertools

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
      if 'Prompt' not in v:
        v['Prompt'] = ''
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

    data = self.config[index.row()]
    if role == Qt.ItemDataRole.DisplayRole:
      if index.column() == 0:
        return data['Image']
      elif index.column() == 1:
        return data['Video']
      elif index.column() == 3:
        return data['Prompt']
    elif role == Qt.ItemDataRole.EditRole:
      if index.column() == 0:
        return data['Image']
      elif index.column() == 1:
        return data['Video']
      elif index.column() == 2:
        return data['Hold']
      elif index.column() == 3:
        return data['Prompt']
    elif role == Qt.ItemDataRole.CheckStateRole:
      if index.column() == 2:
        return Qt.CheckState.Checked if data['Hold'] else Qt.CheckState.Unchecked

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    print('setData', index, value, Qt.CheckState.Checked, role)
    data = self.config[index.row()]
    if index.column() == 0:
      data['Image'] = value
      return True
    elif index.column() == 1:
      data['Video'] = value
      return True
    elif index.column() == 2:
      data['Hold'] = value == Qt.CheckState.Checked.value
      return True
    elif index.column() == 3:
      data['Prompt'] = value
      return True
    return False

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    if index.column() == 2:
      return super().flags(index) | Qt.ItemFlag.ItemIsEditable | Qt.ItemFlag.ItemIsUserCheckable
    else:
      return super().flags(index) | Qt.ItemFlag.ItemIsEditable

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Image"
      elif section == 1:
        return "Video"
      elif section == 2:
        return "Hold"
      elif section == 3:
        return "Prompt"
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
    return 4

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

  add_button.clicked.connect(lambda: transitions.append({'Image': '', 'Video': '', 'Hold': False, 'Prompt': ''}))

  def on_remove():
    row_set = set(item.row() for item in qlist.selectedIndexes())
    row_list = sorted(row_set, reverse=True)
    for row in row_list:
      print('on_remove', row)
      del transitions[row]
  remove_button.clicked.connect(on_remove)

  form = Form.build(task_config, ["Name:", "Value:"],
    Form.Constant('Accumulator Increment', 'accumulator_increment', .015, '', 4),
    Form.Constant('Go Threshold', 'go_threshold', .8, ''),
    Form.Constant('Hold Threshold', 'hold_threshold', .3, ''),
  )
  layout.addWidget(form)
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
accumulator_increment = .015
video_frame = 0

class State(enum.Enum):
  PREP = enum.auto()
  GO = enum.auto()
  SUCCESS = enum.auto()
  FAIL = enum.auto()

async def analog_processor(stream):
  global current_time, accumulator, video_frame
  try:
    async for message in stream:
      for span in message.spans:
        if span.name == 'prediction':
          count = sum(1 for d in message.data[span.begin:span.end] if d > .5)
          accumulator += count*accumulator_increment
          video_frame += count
  finally:
    stream.cancel()

async def load_video(path: pathlib.Path):
  images = []
  proc = await asyncio.create_subprocess_exec(native_exe, 'ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=width,height', '-of', 'csv=s=x:p=0',
                                              str(path),
                                              stdout=asyncio.subprocess.PIPE)
  data = await proc.stdout.read()
  text = data.decode('utf8')
  await proc.wait()
  print(text)
  width, height = [int(s) for s in text.split('x')]
  proc = await asyncio.create_subprocess_exec(native_exe, 'ffmpeg',
                                '-i', str(path),
                                '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:',
                                stdout=asyncio.subprocess.PIPE) 
  try:
    while True:
      data = await proc.stdout.readexactly(3*width*height)
      image = QImage(data, width, height, QImage.Format.Format_RGB888)
      images.append(image)
  except asyncio.IncompleteReadError:
    pass

  return images

@animate(60)
async def run(context: TaskContextProtocol) -> TaskResult:
  global channel, feedback_node, feedback_channel, stub, queue, analog_queue, log_call, LAST_IMAGE, current_pose, xsens_task, last_range, images, time_node, analog_task, last_video_file, accumulator, video_frame, accumulator_increment

  if channel is None:
    channel = context.get_channel('localhost:50050')
    stub = thalamus_pb2_grpc.ThalamusStub(channel)

  task_config = context.task_config
  accumulator_increment = task_config['accumulator_increment']
  go_threshold = task_config['go_threshold']
  hold_threshold = task_config['hold_threshold']

  if analog_task is None:
    request = thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(name='Precision Motor Detection'))
    stream = stub.analog(request)
    analog_task = create_task_with_exc_handling(analog_processor(stream))

  transitions = task_config['Transitions']
  seen_videos = set()
  images = []
  video_tasks = []
  for i, transition in enumerate(transitions):
    video, image = transition['Video'], transition['Image']

    image_path = pathlib.Path(image)
    images.append(load_image(image_path))

    if not video:
      task_config['error'] = f'video for tansition {i} is undefined'

    video_path = pathlib.Path(video)
    if not video_path.exists():
      task_config['error'] = f'video file for tansition {i} does not exist'

    if video not in loaded_videos and video_path not in seen_videos:
      seen_videos.add(video_path)
      async def load_helper(video_path):
        images = await load_video(video_path)
        loaded_videos[str(video_path)] = images

      video_tasks.append(load_helper(video_path))

  await asyncio.gather(*video_tasks)

  config_json = json.dumps(context.task_config.unwrap())
  await context.log(config_json)

  prompt = ''

  def renderer(painter: QPainter):
    font = painter.font()
    font.setPointSize(150)
    painter.setFont(font)
    text_height = 200

    if state == State.PREP:
      painter.fillRect(0, 0, context.widget.width(), context.widget.height(), QColor(255, 255, 0))
      if context.widget.height() <= text_height:
        return
      if not start_image.isNull():
        scale = min((context.widget.width())/start_image.width(), (context.widget.height() - text_height)/start_image.height())
        scaled_width = int(start_image.width()*scale)
        scaled_height = int(start_image.height()*scale)
        painter.drawImage(QRect((context.widget.width() - scaled_width)//2, 0, scaled_width, scaled_height), start_image)

      painter.drawText(QRect(0, context.widget.height()-text_height, context.widget.width(), text_height), Qt.AlignmentFlag.AlignCenter, 'PREP')
    elif state == State.GO:
      color = QColor(255, 0, 0) if is_hold else QColor(0, 255, 0)
      painter.fillRect(0, 0, context.widget.width(), context.widget.height(), color)

      if prompt:
        text = prompt
      else:
        text = 'HOLD' if is_hold else 'GO'
      end_image = video_frames[min(len(video_frames)-1, video_frame)]

      scale = min((context.widget.width())/end_image.width(), (context.widget.height() - text_height)/end_image.height())
      scaled_width = int(end_image.width()*scale)
      scaled_height = int(end_image.height()*scale)
      painter.drawImage(QRect((context.widget.width() - scaled_width)//2, 0, scaled_width, scaled_height), end_image)
      painter.drawText(QRect(0, context.widget.height()-text_height, context.widget.width(), text_height), Qt.AlignmentFlag.AlignCenter, text)
    elif state == State.SUCCESS:
      painter.setPen(Qt.GlobalColor.white)
      painter.drawText(QRect(0, 0, context.widget.width(), context.widget.height()), Qt.AlignmentFlag.AlignCenter, 'SUCCESS')
    elif state == State.FAIL:
      painter.setPen(Qt.GlobalColor.white)
      painter.drawText(QRect(0, 0, context.widget.width(), context.widget.height()), Qt.AlignmentFlag.AlignCenter, 'MISS')


  context.widget.renderer = renderer

  space_pressed = False
  def on_key_release(e: QKeyEvent):
    nonlocal space_pressed
    if e.key() == Qt.Key.Key_Space:
      space_pressed = True

  context.widget.key_release_handler = on_key_release

  future = None
  for i, transition in enumerate(transitions):
    start_filename, end_filename, is_hold = transition['Image'], transition['Video'], transition['Hold']
    prompt = transition['Prompt']

    start_image = images[i]
    video_frames = loaded_videos[end_filename]

    state = State.PREP
    space_pressed = False
    await context.until(lambda: space_pressed)

    state = State.GO
    video_frame = 0
    accumulator = 0
    threshold = hold_threshold if is_hold else go_threshold
    duration = 3 if is_hold else 5
    await context.any(context.sleep(datetime.timedelta(seconds=duration)), context.until(lambda: accumulator >= threshold))

    if is_hold:
      state = State.SUCCESS if accumulator < threshold else State.FAIL
    else:
      state = State.SUCCESS if accumulator >= threshold else State.FAIL

    await context.sleep(datetime.timedelta(seconds=1.5))
    

  return TaskResult(True)
