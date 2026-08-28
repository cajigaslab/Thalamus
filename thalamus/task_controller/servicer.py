
import typing
import asyncio
import logging
import traceback

import grpc
from .. import task_controller_pb2
from .. import task_controller_pb2_grpc
from .. import util_pb2

from .util import TaskContextProtocol

LOGGER = logging.getLogger(__name__)

class ExecutorLostError(RuntimeError):
  def __init__(self):
    super().__init__('Executor disconnected')

class TaskControllerServicer(task_controller_pb2_grpc.TaskControllerServicer):
  def __init__(self):
    super().__init__()
    self.out_queue = asyncio.Queue()
    self.in_queue = asyncio.Queue()
    self.connection_event = asyncio.Event()
    self.stopped_event = asyncio.Event()
    self.next_key = 0
    self.state_subscribers = {}
    self.trial_summary_subscribers = {}
    self.task_context: typing.Optional[TaskContextProtocol] = None
    self.event_loop = asyncio.get_event_loop()

  def stop(self):
    self.stopped_event.set()

  def __get_key(self):
    key = self.next_key
    self.next_key += 1
    return key

  def start_execution(self, request, context):
    assert self.task_context is not None, "task_context is undefined"
    task_context = self.task_context
    self.event_loop.call_soon_threadsafe(lambda: task_context.start())
    return util_pb2.Empty()

  def stop_execution(self, request, context):
    assert self.task_context is not None, "task_context is undefined"
    task_context = self.task_context
    self.event_loop.call_soon_threadsafe(lambda: task_context.stop())
    return util_pb2.Empty()

  async def state(self, 
                  request_iterator: util_pb2.Empty,
                  context: grpc.ServicerContext):
    key = self.__get_key()
    queue = asyncio.Queue()
    self.state_subscribers[key] = queue
    try:
        state = await queue.get()
        yield state
    finally:
      del self.state_subscribers[key]

  async def trial_summary(self, 
                          request_iterator: util_pb2.Empty,
                          context: grpc.ServicerContext):
    key = self.__get_key()
    queue = asyncio.Queue()
    self.trial_summary_subscribers[key] = queue
    try:
        summary = await queue.get()
        yield summary
    finally:
      del self.trial_summary_subscribers[key]

  async def execution(self, 
                      request_iterator: typing.AsyncIterable[task_controller_pb2.TaskConfig],
                      context: grpc.ServicerContext) -> typing.AsyncIterable[task_controller_pb2.TaskResult]:
    LOGGER.info('Executor connected')
    waiting_for_result = False
    self.out_queue = asyncio.Queue()
    try:
      self.connection_event.set()
      out_event = asyncio.create_task(self.out_queue.get())
      in_event = asyncio.create_task(request_iterator.__anext__())
      while True:
        done, _ = await asyncio.wait([out_event, in_event], return_when=asyncio.FIRST_COMPLETED)
        if out_event.done():
          yield await out_event
          out_event = asyncio.create_task(self.out_queue.get())
          waiting_for_result = True
        elif in_event.done():
          await self.in_queue.put(await in_event)
          in_event = asyncio.create_task(request_iterator.__anext__())
          waiting_for_result = False
    except (StopAsyncIteration, asyncio.CancelledError):
      LOGGER.info('Executor disconnected')
      if waiting_for_result:
        await self.in_queue.put(None)
        waiting_for_result = False
    finally:
      out_event.cancel()
      in_event.cancel()
      try:
        await out_event
        await in_event
      except asyncio.CancelledError:
        pass
      self.connection_event.clear()

  async def wait_for_executor(self) -> None:
    got, stopped = asyncio.get_event_loop().create_task(self.connection_event.wait()), asyncio.get_event_loop().create_task(self.stopped_event.wait())
    done, _ = await asyncio.wait([got, stopped], return_when=asyncio.FIRST_COMPLETED)
    if self.stopped_event.is_set():
      raise asyncio.CancelledError()

  async def send_config(self, config: task_controller_pb2.TaskConfig):
    await self.out_queue.put(config)

  async def get_result(self) -> task_controller_pb2.TaskResult:
    got, stopped = asyncio.get_event_loop().create_task(self.in_queue.get()), asyncio.get_event_loop().create_task(self.stopped_event.wait())
    done, _ = await asyncio.wait([got, stopped], return_when=asyncio.FIRST_COMPLETED)
    if self.stopped_event.is_set():
      raise asyncio.CancelledError()

    result = next(iter(done)).result()
    if result is None:
      raise ExecutorLostError()
    return result
  
  async def publish_state(self, value):
    await asyncio.gather(*(v.put(value) for v in self.state_subscribers.values()))
  
  async def publish_trial_summary(self, value):
    await asyncio.gather(*(v.put(value) for v in self.trial_summary_subscribers.values()))
