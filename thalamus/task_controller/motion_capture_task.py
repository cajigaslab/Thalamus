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

from ..config import *
import pathlib
import grpc
from .. import util_pb2
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc

from .widgets import Form
from .util import (
  TaskContextProtocol, UdpProtocol, CanvasPainterProtocol, movella_decorator, TaskResult, MovellaReceiver,
  nidaq_decorator, NidaqmxTaskWrapper, create_task_with_exc_handling)
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

  if 'available_images' not in task_config:
    task_config['available_images'] = []
  for i, pair in enumerate(task_config['available_images']):
    if isinstance(pair, str):
      task_config['available_images'][i] = [pair, pathlib.Path(pair).name]

  if 'selected_images' not in task_config:
    task_config['selected_images'] = []
  for i, pair in enumerate(task_config['selected_images']):
    if isinstance(pair, str):
      task_config['selected_images'][i] = [pair, pathlib.Path(pair).name]

  form = Form.build(task_config, ["Name:", "Value:"],
    #Stimulation parameters
    #Form.Directory('Signs Folders', 'signs_folder', '', 'Select signs folder'),
    #Form.Constant('# Symbols', 'num_symbols', 3, ''),
    Form.Constant('Time per symbol', 'time_per_symbol', 1, 's'),
    Form.Constant('# Iterations', 'num_iterations', 1, ''),
    Form.Bool('Always Randomize', 'always_randomize'),
    Form.Constant('# of Random Images to Select', 'num_random', 1, ''),
    Form.Uniform('Audio Lead (ms)', 'audio_lead', 0, 0, 'ms'),
    Form.File('Start Audio Queue', 'start_audio_file', '', 'Select sound file', ''),
    Form.File('Success Audio Queue', 'success_audio_file', '', 'Select sound file', ''),
    Form.File('Fail Audio Queue', 'fail_audio_file', '', 'Select sound file', ''),
    Form.Bool('Indicate Success/Failure', 'indicate_success_failure', True),
    #Form.Bool('Fixed First', 'fixed_first', True)
  )
  layout.addWidget(form)

  def on_add():
    filenames, file_filter = QFileDialog.getOpenFileNames()
    available_images = task_config['available_images']
    available_images_filenames = set(a[0] for a in available_images)
    new_available_images = list(available_images)

    for f in filenames:
      if f not in available_images:
        new_available_images.append([f, pathlib.Path(f).name])
  
    task_config['available_images'] = new_available_images

  add_button = QPushButton("Add files")
  add_button.clicked.connect(on_add)
  layout.addWidget(add_button)

  list_layout = QHBoxLayout()
  available_qlist, selected_qlist = QTableWidget(0, 2), QTableWidget(0, 2)
  select_button, unselect_button = QPushButton('>'), QPushButton('<')
  available_qlist.setSelectionMode(QAbstractItemView.SelectionMode.ContiguousSelection)
  selected_qlist.setSelectionMode(QAbstractItemView.SelectionMode.ContiguousSelection)
  available_qlist.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
  selected_qlist.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)

  def on_change(action, key, value):
    if key == 'available_images':
      available_qlist.clear()
      available_qlist.setColumnCount(2)
      available_qlist.setRowCount(len(value))
      available_qlist.setHorizontalHeaderLabels(['File', 'Pose'])
      for i, pair in enumerate(value):
        filename, pose = pair
        available_qlist.setItem(i, 0, QTableWidgetItem(filename))
        available_qlist.setItem(i, 1, QTableWidgetItem(pose))
    elif key == 'selected_images':
      selected_qlist.clear()
      selected_qlist.setColumnCount(2)
      selected_qlist.setRowCount(len(value))
      selected_qlist.setHorizontalHeaderLabels(['File', 'Pose'])
      for i, pair in enumerate(value):
        filename, pose = pair
        selected_qlist.setItem(i, 0, QTableWidgetItem(filename))
        selected_qlist.setItem(i, 1, QTableWidgetItem(pose))

  task_config.add_observer(on_change, functools.partial(isdeleted, result))
  on_change(None, 'available_images', task_config['available_images'])
  on_change(None, 'selected_images', task_config['selected_images'])
  
  def on_select():
    selected_images = task_config['selected_images']
    new_selected_images = selected_images.unwrap()

    rows = sorted(set(s.row() for s in available_qlist.selectedItems()))

    for i in rows:
      new_selected_images.append([available_qlist.item(i, 0).text(), available_qlist.item(i, 1).text()])

    task_config['selected_images'] = new_selected_images
  select_button.clicked.connect(on_select)
  
  def on_unselect():
    selected_images = task_config['selected_images']
    new_selected_images = selected_images.unwrap()

    rows = sorted(set(s.row() for s in selected_qlist.selectedItems()))

    for row in rows[::-1]:
      del new_selected_images[row]

    task_config['selected_images'] = new_selected_images
  unselect_button.clicked.connect(on_unselect)

  def on_cell_changed(qlist, field, row, column):
    images = task_config[field]
    new_images = images.unwrap()
    new_value = qlist.item(row, column).text()
    if new_images[row][column] != new_value:
      new_images[row][column] = new_value
      task_config[field] = new_images

  available_qlist.cellChanged.connect(lambda *args: on_cell_changed(available_qlist, 'available_images', *args))
  selected_qlist.cellChanged.connect(lambda *args: on_cell_changed(selected_qlist, 'selected_images', *args))
  
  list_layout.addWidget(available_qlist)
  button_layout = QVBoxLayout()
  button_layout.addWidget(select_button)
  button_layout.addWidget(unselect_button)
  list_layout.addLayout(button_layout)
  list_layout.addWidget(selected_qlist)

  layout.addLayout(list_layout)

  select_random = QPushButton('Select Random')
  random_layout = QHBoxLayout()
  random_layout.addWidget(select_random)
  layout.addLayout(random_layout)

  def on_random():
    available_images = task_config['available_images'].unwrap()
    num_random = round(task_config['num_random'])
    count = min(num_random, len(available_images))
    task_config['selected_images'] = random.sample(available_images, count)

  select_random.clicked.connect(on_random)

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

async def run(context: TaskContextProtocol) -> TaskResult:
  global channel, stub, queue, analog_queue, events_call, LAST_IMAGE, current_pose, xsens_task
  root = context.task_config
  while root.parent:
    root = root.parent
  
  time_per_symbol = datetime.timedelta(seconds=context.get_value('time_per_symbol'))
  num_iterations = int(context.get_value('num_iterations'))
  audio_lead = datetime.timedelta(milliseconds=int(context.get_value('audio_lead', {'min':0, 'max':9})))
  always_randomize = context.task_config['always_randomize']
  start_audio_filename = context.task_config.get('start_audio_file', None)
  success_audio_filename = context.task_config.get('success_audio_file', None)
  fail_audio_filename = context.task_config.get('fail_audio_file', None)
  indicate_success_failure = context.task_config.get('indicate_success_failure', True)
  print('indicate_success_failure', indicate_success_failure)

  start_audio_queue = QSound(start_audio_filename) if start_audio_filename and pathlib.Path(start_audio_filename).exists() else None
  success_audio_queue = QSound(success_audio_filename) if success_audio_filename and pathlib.Path(success_audio_filename).exists() else None
  fail_audio_queue = QSound(fail_audio_filename) if fail_audio_filename and pathlib.Path(fail_audio_filename).exists() else None

  blank_time = datetime.timedelta(milliseconds=32)

  if always_randomize:
    available_images = context.task_config['available_images']
    num_random = round(context.task_config['num_random'])
    count = min(num_random, len(available_images))
    choices = []
    for i in range(count):
      selected = LAST_IMAGE
      while selected is LAST_IMAGE:
        selected = random.choice(available_images)
      choices.append(selected)
      LAST_IMAGE = selected
    context.task_config['selected_images'] = choices

  maybe_images = [load_image(p[0]) for p in context.task_config['selected_images']]
  maybe_poses = [p[1] for p in context.task_config['selected_images']]
  valid_indexes = [i for i in range(len(maybe_images)) if not maybe_images[i].isNull()]
  images = [i for i in maybe_images if not i.isNull()]
  context.widget.load_images(images)

  if not valid_indexes:
    def null_renderer(painter: CanvasPainterProtocol) -> None:
      text = 'No images to render'

      metrics = QFontMetrics(painter.font())
      bounds = metrics.boundingRect(text)

      painter.setPen(Qt.GlobalColor.white)
      painter.drawText((context.widget.width() - bounds.width())//2,
                       (context.widget.height() - bounds.height())//2, text)

    context.widget.renderer = null_renderer
    context.widget.update()
    await context.sleep(datetime.timedelta(seconds=1))

  state = State.QUEUE
  def renderer(painter: CanvasPainterProtocol) -> None:
    nonlocal rendered_images
    painter.fillRect(0, 0, context.widget.width(), context.widget.height(), Qt.GlobalColor.black)
    font = painter.font()
    font.setPointSize(192)
    painter.setFont(font)
    if state == State.SUCCESS:
      if indicate_success_failure:
        painter.setPen(QColor(0, 255, 0))
        painter.drawText(QRect(0, 0, context.widget.width(), context.widget.height()), Qt.AlignmentFlag.AlignCenter, 'SUCCESS')
      return
    elif state == State.FAILURE:
      if indicate_success_failure:
        painter.setPen(QColor(255, 0, 0))
        painter.drawText(QRect(0, 0, context.widget.width(), context.widget.height()), Qt.AlignmentFlag.AlignCenter, 'FAILURE')
      return

    if not rendered_images:
      return
    images_width = sum(i.width() for i in rendered_images)
    images_height = max(i.height() for i in rendered_images)
    scale = min(context.widget.width()/images_width, context.widget.height()/images_height)
    
    offset = [(context.widget.width() - images_width*scale)/2, (context.widget.height() - images_height*scale)/2]
    for image in rendered_images:
      painter.drawImage(QRect(int(offset[0]), int(offset[1]), int(image.width()*scale), int(image.height()*scale)), image)
      offset[0] += image.width()*scale

  space_pressed = False
  def on_key_release(e: QKeyEvent):
    nonlocal space_pressed
    space_pressed = space_pressed or e.key() == Qt.Key.Key_Space

  context.widget.renderer = renderer
  context.widget.key_release_handler = on_key_release
  rendered_images = []

  async def xsens_processor():
    global current_pose
    async for response in xsens_call:
      current_pose = response.pose_name
  
  if channel is None:
    channel = context.get_channel('localhost:50050')
    stub = thalamus_pb2_grpc.ThalamusStub(channel)
    queue = IterableQueue()
    events_call = stub.events(queue)
    analog_queue = IterableQueue()
    events_call = stub.inject_analog(analog_queue)
    await analog_queue.put(thalamus_pb2.InjectAnalogRequest(node="gesture_signal"))
    xsens_call = stub.xsens(thalamus_pb2.NodeSelector(type='XSENS'))
    xsens_task = create_task_with_exc_handling(xsens_processor())

  config_json = json.dumps(context.task_config.unwrap())
  await queue.put(thalamus_pb2.Event(payload=config_json.encode(),time=time.perf_counter_ns()))

  async def pulse():
    await analog_queue.put(thalamus_pb2.InjectAnalogRequest(signal=thalamus_pb2.AnalogResponse(
        data=[5],
        spans=[thalamus_pb2.Span(begin=0,end=1)],
        sample_intervals=[0])))
    await context.sleep(datetime.timedelta(seconds=.1))
    await analog_queue.put(thalamus_pb2.InjectAnalogRequest(signal=thalamus_pb2.AnalogResponse(
        data=[0],
        spans=[thalamus_pb2.Span(begin=0,end=1)],
        sample_intervals=[0])))
    
  async def show_images(indexes, duration: typing.Optional[datetime.timedelta] = None):
    nonlocal rendered_images, space_pressed
    rendered_images = [maybe_images[i] for i in indexes]
    context.widget.update()

    payload = bytes(indexes)
    tasks = [
      asyncio.get_event_loop().create_task(queue.put(thalamus_pb2.Event(payload=payload,time=time.perf_counter_ns()))),
    ]

    if duration is None:
      tasks.append(context.until(lambda: space_pressed))
    else:
      tasks.append(context.sleep(duration))

    if indexes:
      tasks.append(create_task_with_exc_handling(pulse()))

    await asyncio.gather(*tasks)
    space_pressed = False

  for i in range(num_iterations):

    for i in valid_indexes:
      state = State.QUEUE
      if start_audio_queue is not None:
        start_audio_queue.play()
      await show_images([], audio_lead)

      await show_images([i])

      state = State.SUCCESS if current_pose == maybe_poses[i] else State.FAILURE
      context.widget.update()
      if indicate_success_failure:
        if state == State.SUCCESS:
          if success_audio_queue:
            success_audio_queue.play()
        else:
          if fail_audio_queue:
            fail_audio_queue.play()
      await context.sleep(datetime.timedelta(seconds=1))

  return TaskResult(True)
