import json
import typing
import asyncio
import logging
import traceback
import threading

import grpc

from . import thalamus_pb2
from . import thalamus_pb2_grpc
from .iterable_queue import IterableQueue
from .config import ObservableCollection, ObservableDict, ObservableList
import jsonpath_ng

LOGGER = logging.getLogger(__name__)

class MatchContext(typing.NamedTuple):
  value: ObservableCollection

class PatchMatch(typing.NamedTuple):
  value: typing.Optional[ObservableCollection]
  context: MatchContext
  path: jsonpath_ng.Child

class ThalamusThread:
  def __init__(self, address: str):
    self.address = address
    self.thread: typing.Optional[threading.Thread] = None
    self.stub: typing.Optional[thalamus_pb2_grpc.ThalamusStub] = None
    self.running = False
    self.condition = threading.Condition()
    self.async_condition = asyncio.Condition()
    self.loop: typing.Optional[asyncio.AbstractEventLoop] = None
    self.config = ObservableDict({})
    self.pending_callbacks = {}
    self.next_id = 1
    self.queue: typing.Optional[IterableQueue] = None

  def send_change(self, action: ObservableCollection.Action, address: str, value: typing.Any, callback: typing.Callable[[], None]) -> bool:
    assert self.queue is not None

    transaction = thalamus_pb2.ObservableTransaction(
      changes = [thalamus_pb2.ObservableChange(
        address = address,
        value = json.dumps(value),
        action = thalamus_pb2.ObservableChange.Action.Set
      )],
      id = self.next_id
    )
    self.next_id += 1
    self.pending_callbacks[transaction.id] = callback

    asyncio.get_event_loop().create_task(self.queue.put(transaction))

    return True

  async def __async_main(self):
    try:
      self.loop = asyncio.get_event_loop()
      async with grpc.aio.insecure_channel(self.address) as channel:
        await channel.channel_ready()

        stub = thalamus_pb2_grpc.ThalamusStub(channel)
        bridge_channel = channel
        bridge_stub = stub
        while self.running:
          self.queue = IterableQueue()
          stream = bridge_stub.observable_bridge_v2(self.queue)
          async for transaction in stream:
            if not self.running:
              break
            if transaction.redirection:
              bridge_channel = grpc.aio.insecure_channel(transaction.redirection)
              await bridge_channel.channel_ready()
              bridge_stub = thalamus_pb2_grpc.ThalamusStub(bridge_channel)
              break

            if transaction.acknowledged:
              callback = self.pending_callbacks[transaction.acknowledged]
              del self.pending_callbacks[transaction.acknowledged]
              callback()
              continue

            for change in transaction.changes:
              if change.address == '':
                value = json.loads(change.value)
                self.config = ObservableDict(value)
                self.config.set_remote_storage(self.send_change)
                with self.condition:
                  async with self.async_condition:
                    self.stub = stub
                    self.condition.notify_all()
                    self.async_condition.notify_all()
                continue

              try:
                jsonpath_expr = jsonpath_ng.parse(change.address)
              except Exception as _exc: # pylint: disable=broad-except
                LOGGER.exception('Failed to parse JSONPATH %s', change.address)
                continue
              
              matches = jsonpath_expr.find(self.config)

              if not matches:
                if isinstance(jsonpath_expr, jsonpath_ng.Child):
                  for m in jsonpath_expr.left.find(self.config):
                    matches.append(PatchMatch(None, MatchContext(m.value), jsonpath_expr.right))
                else:
                  matches.append(PatchMatch(None, MatchContext(self.config), jsonpath_expr))

              try:
                value = json.loads(change.value)
              except json.JSONDecodeError:
                LOGGER.exception('Failed to decode JSON: %s', change.value)
                continue

              for match in matches:
                if isinstance(match.value, ObservableCollection):
                  match.value.assign(value, from_remote=True)
                elif isinstance(match.path, jsonpath_ng.Index):
                  if match.path.index == len(match.context.value):
                    match.context.value.append(value, from_remote=True)
                  else:
                    match.context.value.setitem(match.path.index, value, lambda: None, True)
                elif isinstance(match.path, jsonpath_ng.Fields):
                  match.context.value.setitem(match.path.fields[0], value, lambda: None, True)
    except asyncio.CancelledError:
      pass
    except grpc.aio.AioRpcError:
      pass
    except:
      print('============================================')
      traceback.print_exc()
    finally:
      self.running = False
      self.stub = None
      self.loop = None


  def __main(self):
    asyncio.run(self.__async_main())

  def start(self):
    self.thread = threading.Thread(target=self.__main)
    self.running = True
    self.thread.start()
    with self.condition:
      self.condition.wait_for(lambda: self.stub is not None)

  async def async_start(self):
    self.running = True
    task = asyncio.get_event_loop().create_task(self.__async_main())
    async with self.async_condition:
      await self.async_condition.wait_for(lambda: self.stub is not None)
    return task


  def stop(self):
    if self.thread is not None:
      self.running = False
      self.thread.join()

