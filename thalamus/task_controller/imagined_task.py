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
      return data['Video']
    elif index.column() == 1:
      return data['Goal']
    elif index.column() == 2:
      return data['Duration']

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role != Qt.ItemDataRole.EditRole:
      return super().setData(index, value, role)

    data = self.config[index.row()]
    if index.column() == 0:
      data['Video'] = value
      return True
    elif index.column() == 1:
      data['Goal'] = value
      return True
    elif index.column() == 2:
      try:
        data['Duration'] = float(value)
      except ValueError:
        return False
      return True
    return False

  def flags(self, index: QModelIndex) -> Qt.ItemFlag:
    return super().flags(index) | Qt.ItemFlag.ItemIsEditable

  def headerData(self, section: int, orientation: Qt.Orientation, role: int):
    if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
      if section == 0:
        return "Video"
      elif section == 1:
        return "Goal"
      elif section == 2:
        return "Duration (s)"
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

  add_button.clicked.connect(lambda: transitions.append({'Video': '', 'Goal': '', 'Duration': 1.0}))

  def on_remove():
    row_set = set(item.row() for item in qlist.selectedIndexes())
    row_list = sorted(row_set, reverse=True)
    for row in row_list:
      print('on_remove', row)
      del transitions[row]
  remove_button.clicked.connect(on_remove)

  form = Form.build(task_config, ["Name:", "Value:"],
    Form.Constant('Time Between Transisions (s)', 'inter_transition_interval', 1, 's'),
    Form.File('Audio Queue', 'audio_file', '', 'Select sound file', ''),
    Form.String('Feedback Node', 'feedback_node', ''),
    Form.String('Feedback Channel', 'feedback_channel', '')
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

@functools.lru_cache(maxsize=None)
def load_image(path: pathlib.Path):
  return QImage(str(path))

@functools.lru_cache(maxsize=None)
def load_sound(path: pathlib.Path):
  return QSound(str(path))

last_range = None
images = []
time_node = ''
analog_task = None
current_time = 0.0
last_video_file = ''
loaded_videos = {}
feedback_node = ''
feedback_channel = ''

async def analog_processor(stream):
  global current_time
  try:
    async for message in stream:
      if len(message.data) == 0:
        continue
      current_time = min(max(0, message.data[-1]), 1)
  finally:
    stream.cancel()

@animate(60)
async def run(context: TaskContextProtocol) -> TaskResult:
  global channel, feedback_node, feedback_channel, stub, queue, analog_queue, log_call, LAST_IMAGE, current_pose, xsens_task, last_range, images, time_node, analog_task, last_video_file

  if channel is None:
    channel = context.get_channel('localhost:50050')
    stub = thalamus_pb2_grpc.ThalamusStub(channel)
    analog_queue = IterableQueue()
    events_call = stub.inject_analog(analog_queue)
    await analog_queue.put(thalamus_pb2.InjectAnalogRequest(node="gesture_signal"))

  task_config = context.task_config

  new_feedback_node, new_feedback_channel = task_config['feedback_node'], task_config['feedback_channel']
  if (new_feedback_node, new_feedback_channel) != (feedback_node, feedback_channel):
    feedback_node = new_feedback_node
    feedback_channel = new_feedback_channel
    if analog_task:
      analog_task.cancel()
    if not feedback_node or not feedback_channel:
      analog_task = None
    else:
      request = thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(name=feedback_node),channel_names=[feedback_channel])
      stream = stub.analog(request)
      analog_task = create_task_with_exc_handling(analog_processor(stream))

  transitions = task_config['Transitions']
  inter_interval = datetime.timedelta(seconds=task_config['inter_transition_interval'])

  audio_path = pathlib.Path(task_config['audio_file'])
  sound = None
  if audio_path.exists():
    sound = load_sound(audio_path)

  video_tasks = []
  goals = []
  task_config['error'] = ''
  seen_videos = set()
  for i, transition in enumerate(transitions):
    video, goal = transition['Video'], transition['Goal']

    if not goal:
      task_config['error'] = f'goal image for tansition {i} is undefined'

    goal_path = pathlib.Path(goal)
    if not goal_path.exists():
      task_config['error'] = f'goal image file for tansition {i} is undefined'

    goals.append(load_image(goal))

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

  if task_config['error']:
    await context.sleep(datetime.timedelta(seconds=1))
    return TaskResult(False)

  config_json = json.dumps(context.task_config.unwrap())
  await context.log(config_json)

  def renderer(painter):
    if not draw:
        return

    elapsed = time.perf_counter() - start_time
    index = int(elapsed/duration*(len(frames)-1))
    index = min(max(index, 0), len(frames)-1)
    frame = frames[index]

    if analog_task is not None:
      portion = 1/3
      feedback_index = int(current_time*(len(frames)-1))
      feedback_index = min(max(feedback_index, 0), len(frames)-1)
      feedback_frame = frames[feedback_index]

      scale = min((portion*context.widget.width())/frame.width(), (context.widget.height())/frame.height())
      painter.drawImage(QRect(0, int((context.widget.height() - scale*frame.height())//2),
                              int(scale*frame.width()), int(scale*frame.height())),
                        feedback_frame)

      scale = min((portion*context.widget.width())/frame.width(), (context.widget.height())/frame.height())
      painter.drawImage(QRect(int(portion*context.widget.width()), int((context.widget.height() - scale*frame.height())//2),
                              int(scale*frame.width()), int(scale*frame.height())),
                        frame)

      scale = min((portion*context.widget.width())/goal.width(), (context.widget.height())/goal.height())
      painter.drawImage(QRect(2*int(portion*context.widget.width()), int((context.widget.height() - scale*goal.height())//2),
                              int(scale*goal.width()), int(scale*goal.height())),
                        goal)
    else:
      portion = 1

      scale = min(context.widget.width()/frame.width(), context.widget.height()/frame.height())
      painter.drawImage(QRect(int((context.widget.width() - scale*frame.width())//2), int((context.widget.height() - scale*frame.height())//2),
                              int(scale*frame.width()), int(scale*frame.height())),
                        frame)

      scale = min((context.widget.width()/4)/goal.width(), (context.widget.height()/4)/goal.height())
      painter.drawImage(QRect(int(context.widget.width() - scale*goal.width()), 0,
                              int(scale*goal.width()), int(scale*goal.height())),
                        goal)

    

    if elapsed >= duration and future is not None:
      future.set_result(None)

  context.widget.renderer = renderer

  space_pressed = False
  def on_key_release(e: QKeyEvent):
    nonlocal space_pressed
    space_pressed = space_pressed or e.key() == Qt.Key.Key_Space

  context.widget.key_release_handler = on_key_release

  future = None
  draw = False
  for i, transition in enumerate(transitions):
    draw = False
    space_pressed = False
    await context.sleep(inter_interval)
    print(sound, sound.source(), sound.status())
    #if sound is not None:
    sound.play()
    await asyncio.gather(
      analog_queue.put(thalamus_pb2.InjectAnalogRequest(signal=thalamus_pb2.AnalogResponse(
        data=[5,0],
        spans=[thalamus_pb2.Span(begin=0,end=2)],
        sample_intervals=[100000000]))),
      context.log(f'{i} start'))

    
    draw = True
    start_time = time.perf_counter()
    video, duration = transition['Video'], transition['Duration']
    frames = loaded_videos[video]
    goal = goals[i]
    future = asyncio.get_event_loop().create_future()
    await future
    future = None
    await context.log(f'{i} end')
    await context.until(lambda: space_pressed)

  return TaskResult(True)
