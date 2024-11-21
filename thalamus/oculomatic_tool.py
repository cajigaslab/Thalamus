import sys
import typing
import asyncio
import traceback
import threading
import subprocess
import multiprocessing
import multiprocessing.connection

from thalamus.qt import *
from .thread import ThalamusThread
from .config import ObservableDict

from . import thalamus_pb2
from . import thalamus_pb2_grpc

import dataclasses

@dataclasses.dataclass
class Value:
  x: float
  y: float
  diameter: float

@dataclasses.dataclass
class Scale:
  quadrant: str
  x: float
  y: float

class OculomaticWidget(QMainWindow):
  def __init__(self, queue: multiprocessing.connection.Connection):
    super().__init__()
    central_widget = QWidget()
    layout = QGridLayout()
    layout.addWidget(QLabel('I'), 0, 0, 1, 2)
    layout.addWidget(QLabel('II'), 0, 2, 1, 2)
    layout.addWidget(QLabel('III'), 2, 0, 1, 2)
    layout.addWidget(QLabel('IV'), 2, 2, 1, 2)
    central_widget.setLayout(layout)
    self.setCentralWidget(central_widget)

    i_x = QDoubleSpinBox()
    i_y = QDoubleSpinBox()
    ii_x = QDoubleSpinBox()
    ii_y = QDoubleSpinBox()
    iii_x = QDoubleSpinBox()
    iii_y = QDoubleSpinBox()
    iv_x = QDoubleSpinBox()
    iv_y = QDoubleSpinBox()
    i_x.setValue(1)
    i_y.setValue(1)
    ii_x.setValue(1)
    ii_y.setValue(1)
    iii_x.setValue(1)
    iii_y.setValue(1)
    iv_x.setValue(1)
    iv_y.setValue(1)
    layout.addWidget(i_x, 1, 0)
    layout.addWidget(i_y, 1, 1)
    layout.addWidget(ii_x, 1, 2)
    layout.addWidget(ii_y, 1, 3)
    layout.addWidget(iii_x, 3, 0)
    layout.addWidget(iii_y, 3, 1)
    layout.addWidget(iv_x, 3, 2)
    layout.addWidget(iv_y, 3, 3)
    self.silent = False

    def value_changed(w, v):
      if self.silent:
        return
      if w is i_x:
        queue.send(('I', 'x', i_x.value()))
      elif w is i_y:
        queue.send(('I', 'y', i_y.value()))
      elif w is ii_x:
        queue.send(('II', 'x', ii_x.value()))
      elif w is ii_y:
        queue.send(('II', 'y', ii_y.value()))
      elif w is iii_x:
        queue.send(('III', 'x', iii_x.value()))
      elif w is iii_y:
        queue.send(('III', 'y', iii_y.value()))
      elif w is iv_x:
        queue.send(('IV', 'x', iv_x.value()))
      elif w is iv_y:
        queue.send(('IV', 'y', iv_y.value()))
    
    i_x.valueChanged.connect(lambda v: value_changed(i_x, v))
    i_y.valueChanged.connect(lambda v: value_changed(i_y, v))
    ii_x.valueChanged.connect(lambda v: value_changed(ii_x, v))
    ii_y.valueChanged.connect(lambda v: value_changed(ii_y, v))
    iii_x.valueChanged.connect(lambda v: value_changed(iii_x, v))
    iii_y.valueChanged.connect(lambda v: value_changed(iii_y, v))
    iv_x.valueChanged.connect(lambda v: value_changed(iv_x, v))
    iv_y.valueChanged.connect(lambda v: value_changed(iv_y, v))

    def on_timeout():
      if not queue.poll():
        return

      try:
        self.silent = True
        quadrant, axis, value = queue.recv()
        if quadrant == 'I':
          if axis == 'x':
            i_x.setValue(value)
          else:
            i_y.setValue(value)
        elif quadrant == 'II':
          if axis == 'x':
            ii_x.setValue(value)
          else:
            ii_y.setValue(value)
        elif quadrant == 'III':
          if axis == 'x':
            iii_x.setValue(value)
          else:
            iii_y.setValue(value)
        elif quadrant == 'IV':
          if axis == 'x':
            iv_x.setValue(value)
          else:
            iv_y.setValue(value)
      finally:
        self.silent = False

    self.timer = QTimer()
    self.timer.timeout.connect(on_timeout)
    self.timer.start(100)

def ui_process(queue: multiprocessing.connection.Connection):
  app = QApplication(sys.argv)
  window = OculomaticWidget(queue)
  window.show()
  app.exec()

loop: asyncio.AbstractEventLoop

class OculomaticTool():
  def __init__(self, arg1: typing.Union[ThalamusThread, asyncio.BaseEventLoop, thalamus_pb2_grpc.ThalamusStub],
                     arg2: typing.Optional[typing.Union[thalamus_pb2_grpc.ThalamusStub, ObservableDict]] = None,
                     arg3: typing.Optional[ObservableDict] = None,
                     ):
    self.loop: asyncio.AbstractEventLoop
    self.stub: thalamus_pb2_grpc.ThalamusStub
    self.config: ObservableDict
    if isinstance(arg1, thalamus_pb2_grpc.ThalamusStub):
      self.loop = asyncio.get_running_loop()
      self.stub = arg1
      assert isinstance(arg2, ObservableDict)
      self.config = arg2
    elif isinstance(arg1, asyncio.BaseEventLoop):
      self.loop = arg1
      assert isinstance(arg2, thalamus_pb2_grpc.ThalamusStub)
      self.stub = arg2
      assert isinstance(arg3, ObservableDict)
      self.config = arg3
    elif isinstance(arg1, ThalamusThread):
      assert arg1.loop is not None
      assert arg1.stub is not None
      assert arg1.config is not None
      self.loop = arg1.loop
      self.stub = arg1.stub
      self.config = arg1.config

    self.lock = threading.Lock()
    self.__value = Value(0,0,0)
    self.scales = {
      'I': Scale('I', 1,1),
      'II': Scale('II', 1,1),
      'III': Scale('III', 1,1),
      'IV': Scale('IV', 1,1)
    }
    local, remote = multiprocessing.Pipe()
    self.local = local
    self.remote = remote
    self.process: typing.Optional[multiprocessing.Process] = None

  def on_eye_change(self, eye, a, k, v):
    scale = self.scales[eye]
    if k == 'x':
      scale.x = v
    else:
      scale.y = v
    self.local.send((eye, k, v))

  def on_eye_config_change(self, a, k, v):
    print(a, k, v)
    if k in ('I', 'II', 'III', 'IV'):
      v.add_observer(lambda a2, k2, v2: self.on_eye_change(k, a2, k2, v2))
      v.recap(lambda a2, k2, v2: self.on_eye_change(k, a2, k2, v2))

  def on_config_change(self, a, k, v):
    if k == 'eye_config':
      v.add_observer(self.on_eye_config_change)
      v.recap(self.on_eye_config_change)

  def start(self):
    def on_loop():
      try:
        self.config.add_observer(self.on_config_change)
        self.config.recap(self.on_config_change)

        if 'eye_config' not in self.config:
          self.config.setitem('eye_config', {
            'I': {'x': 1, 'y': 1},
            'II': {'x': 1, 'y': 1},
            'III': {'x': 1, 'y': 1},
            'IV': {'x': 1, 'y': 1},
          })
        else:
          eye_config = self.config['eye_config']
          if 'I' not in eye_config:
            self.config['I'] = {'x': 1, 'y': 1}
          if 'II' not in eye_config:
            self.config['II'] = {'x': 1, 'y': 1}
          if 'III' not in eye_config:
            self.config['III'] = {'x': 1, 'y': 1}
          if 'IV' not in eye_config:
            self.config['IV'] = {'x': 1, 'y': 1}

        self.loop.create_task(self.__oculomatic_loop())
        self.loop.create_task(self.__ui_loop())

        #multiprocessing.set_start_method('spawn')
        self.process = multiprocessing.Process(target=ui_process, args=[self.remote])
        self.process.start()
      except:
        traceback.print_exc()
    self.loop.call_soon_threadsafe(on_loop)

  @property
  def value(self):
    with self.lock:
      return self.__value

  async def __ui_loop(self):
    try:
      while True:
        if not self.local.poll():
          await asyncio.sleep(.1)
          continue
        change = self.local.recv()
        print(change)
        quadrant, axis, value = change
        scale = self.scales[quadrant]
        quadrant = self.config.get('eye_config', {}).get(quadrant, {})
        if axis == 'x':
          scale.x = value
          quadrant['x'] = scale.x
        else:
          scale.y = value
          quadrant['y'] = scale.y
    except:
      traceback.print_exc()

  async def __oculomatic_loop(self):
    try:
      request = thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(type='OCULOMATIC'))
      stream = self.stub.analog(request)
      try:
        async for message in stream:
          x, y, diameter = 0, 0, 0
          for span in message.spans:
            if span.name == 'X':
              x = message.data[span.begin]
            elif span.name == 'Y':
              y = message.data[span.begin]
            elif span.name == 'Diameter':
              diameter = message.data[span.begin]
          if y >= 0:
            if x >= 0:
              quadrant = 'I'
            else:
              quadrant = 'II'
          else:
            if x >= 0:
              quadrant = 'III'
            else:
              quadrant = 'IV'
          scale = self.scales[quadrant]

          with self.lock:
            self.__value = Value(x*scale.x, y*scale.y, diameter)
      except asyncio.CancelledError:
        pass
    except:
      traceback.print_exc()