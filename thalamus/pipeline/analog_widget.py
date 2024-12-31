from ..qt import *
from ..util import IterableQueue
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from ..task_controller.util import create_task_with_exc_handling
import asyncio
import time

class AnalogWidget(QWidget):
  def __init__(self, config, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()
    self.config = config
    self.stub = stub
    self.holding = True
    self.position = -5e6, -5e6
    self.loop_task = create_task_with_exc_handling(self.__loop(config, stub))

  def closeEvent(self, e):
    print('AnalogWidget.closeEvent')
    self.loop_task.cancel()

  async def __loop(self, config, stub):
    try:
      queue = IterableQueue()
      self.stream = self.stub.inject_analog(queue)
      await queue.put(thalamus_pb2.InjectAnalogRequest(node=config['name']))
      start = time.perf_counter()
      next_time = 0.0
      while True:
        await asyncio.sleep(.016)
        if not config['Widget is Touchpad']:
          start = time.perf_counter()
          next_time = 0.0
          continue

        elapsed = time.perf_counter() - start
        while next_time <= elapsed:
          next_time += .016
          message = thalamus_pb2.InjectAnalogRequest(signal=thalamus_pb2.AnalogResponse(
            data=self.position,
            spans=[thalamus_pb2.Span(begin=0,end=1,name='X'),thalamus_pb2.Span(begin=1,end=2,name='Y')],
            sample_intervals=[16_000_000, 16_000_000]
          ))
          await queue.put(message)
    except asyncio.CancelledError:
      pass

  def mousePressEvent(self, e: QMouseEvent):
    self.holding = True
    self.position = qt_get_x(e), qt_get_y(e)
    pass

  def mouseReleaseEvent(self, e: QMouseEvent):
    self.holding = False
    self.position = -5e6, -5e6
    pass

  def mouseMoveEvent(self, e: QMouseEvent):
    if self.holding:
      self.position = qt_get_x(e), qt_get_y(e)
