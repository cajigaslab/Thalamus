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

print(native_exe, ObservableCollection, thalamus_pb2)

from .widgets import Form
from .util import (
  TaskContextProtocol, UdpProtocol, CanvasPainterProtocol, movella_decorator, TaskResult, MovellaReceiver,
  nidaq_decorator, NidaqmxTaskWrapper, create_task_with_exc_handling, animate)
from ..util import IterableQueue

from ..qt import *

LOGGER = logging.getLogger(__name__)

def create_widget(task_config: ObservableCollection) -> QWidget:
  """
  Returns a QWidget that will be used to edit the task configuration
  """
  result = QWidget()
  layout = QVBoxLayout()
  result.setLayout(layout)

  form = Form.build(task_config, ["Name:", "Value:"],
    Form.String('Time Node', 'time_node'),
    Form.File('Video File', 'video_file', '', 'Select video file', ''),
    Form.Uniform('Time Range', 'time_range', 0, 1, 's'),
  )
  layout.addWidget(form)

  return result

channel = None
stub = None
queue = None
analog_queue = None
events_call = None
LAST_IMAGE = None
current_pose = None
xsens_task = None

@functools.lru_cache(maxsize=None)
def load_image(path: pathlib.Path):
  return QImage(str(path))

class State(enum.Enum):
  QUEUE = enum.auto()
  SUCCESS = enum.auto()
  FAILURE = enum.auto()

class VideoSurface(QAbstractVideoSurface):
  def __init__(self, widget: QWidget):
    super().__init__()
    self.widget = widget
    self.frame: typing.Optional[QVideoFrame] = None

  def present(self, frame):
    #print('present')
    self.frame = frame
    self.widget.update()
    return True

  def supportedPixelFormats(self, type):
    return list(range(35))

last_range = None
images = []
time_node = ''
analog_task = None
current_time = 0.0
last_video_file = ''

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
  global channel, stub, queue, analog_queue, events_call, LAST_IMAGE, current_pose, xsens_task, last_range, images, time_node, analog_task, last_video_file
  root = context.task_config
  while root.parent:
    root = root.parent

  video_file = pathlib.Path(context.task_config['video_file'])
  if not video_file.is_file():
    await context.sleep(datetime.timedelta(seconds=1))
    return TaskResult(False)

  if channel is None:
    channel = context.get_channel('localhost:50050')
    stub = thalamus_pb2_grpc.ThalamusStub(channel)

  new_time_node = context.task_config['time_node']
  if time_node != new_time_node:
    time_node = new_time_node
    if analog_task:
      analog_task.cancel()
    request = thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(name=time_node))
    stream = stub.analog(request)
    analog_task = create_task_with_exc_handling(analog_processor(stream))

  time_range = context.task_config['time_range']['min'], context.task_config['time_range']['max']
  time_range_ints = int(1000*time_range[0]), int(1000*time_range[1])
  if last_range != time_range_ints:
    images = []
    proc = await asyncio.create_subprocess_exec(native_exe, 'ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=width,height', '-of', 'csv=s=x:p=0',
                                                str(video_file),
                                                stdout=asyncio.subprocess.PIPE)
    data = await proc.stdout.read()
    text = data.decode('utf8')
    await proc.wait();
    print(text)
    width, height = [int(s) for s in text.split('x')]
    #await context.sleep(datetime.timedelta(seconds=1))
    #return TaskResult(False)
    proc = await asyncio.create_subprocess_exec(native_exe, 'ffmpeg',
                                  '-i', str(video_file), '-ss', str(time_range[0]), '-to', str(time_range[1]),
                                  '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:',
                                  stdout=asyncio.subprocess.PIPE) 
    try:
      while True:
        data = await proc.stdout.readexactly(3*width*height)
        image = QImage(data, width, height, QImage.Format_RGB888)
        images.append(image)
    except asyncio.IncompleteReadError:
      pass
    print('len(images)', len(images))
    await proc.wait()
    print('proc.wait')
  last_range = time_range_ints
  last_video_file = video_file
   
  #nativeTHALAMUS_ANCHOR / 'native.exe'

  state = State.QUEUE
  def renderer(painter: CanvasPainterProtocol) -> None:
    #print('renderer')
    if not images:
      return

    index = int((len(images)-1)*current_time)
    image = images[index]
    scales = context.widget.width()/image.width(), context.widget.height()/image.height()
    scale = min(scales)

    painter.scale(scale, scale)
    painter.drawImage(0, 0, image)

  space_pressed = False
  def on_key_release(e: PyQt5.QtGui.QKeyEvent):
    nonlocal space_pressed
    space_pressed = space_pressed or e.key() == Qt.Key.Key_Space

  context.widget.renderer = renderer
  context.widget.key_release_handler = on_key_release

  #async def xsens_processor():
  #  global current_pose
  #  async for response in xsens_call:
  #    current_pose = response.pose_name
  #
  #if channel is None:
  #  channel = context.get_channel('localhost:50050')
  #  stub = thalamus_pb2_grpc.ThalamusStub(channel)
  #  queue = IterableQueue()
  #  events_call = stub.events(queue)
  #  analog_queue = IterableQueue()
  #  events_call = stub.inject_analog(analog_queue)
  #  await analog_queue.put(thalamus_pb2.InjectAnalogRequest(node="gesture_signal"))
  #  xsens_call = stub.xsens(thalamus_pb2.NodeSelector(type='XSENS'))
  #  xsens_task = create_task_with_exc_handling(xsens_processor())

  await context.until(lambda: space_pressed)

  return TaskResult(True)
