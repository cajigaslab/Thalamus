import json
import typing
import asyncio
import logging
import traceback

import jsonpath_ng

from . import thalamus_pb2
from . import thalamus_pb2_grpc
from . import util_pb2
from .config import ObservableCollection, ObservableDict, ObservableList
from .task_controller.util import create_task_with_exc_handling

LOGGER = logging.getLogger(__name__)

class MatchContext(typing.NamedTuple):
  value: ObservableCollection

class PatchMatch(typing.NamedTuple):
  value: typing.Optional[ObservableCollection]
  context: MatchContext
  path: jsonpath_ng.Child

STOP = object()

class ThalamusServicer(thalamus_pb2_grpc.ThalamusServicer):
  def __init__(self, config):
    super().__init__()
    self.config = config
    self.queues = []
    self.peer_name_to_queue = {}
    self.condition = asyncio.Condition()
    self.install_observer(config)

  async def stop(self):
    for queue in self.queues:
      await queue.put(STOP)

  def __combine(self, pre: str, end: typing.Union[str, int]) -> str:
    if pre is None:
      if isinstance(end, str):
        return f'[\'{end}\']'
      else:
        return f'[{end}]'
    if isinstance(end, str):
      return f'{pre}[\'{end}\']'
    else:
      return f'{pre}[{end}]'

  def compute_address(self, current: ObservableCollection):
    reverse_path = []
    while current is not None and current.parent is not None:
      items = current.parent.items() if isinstance(current.parent, ObservableDict) else enumerate(current.parent)
      for k, v in items:
        if v is current:
          reverse_path.append(k)
          break
      current = current.parent

    if current is not self.config:
      return None

    path = reverse_path[::-1]
    return ''.join(f'[{repr(p)}]' for p in path)

  def install_observer(self, root):
    the_open = [root]
    the_closed = []

    while the_open:
      current = the_open.pop()
      the_closed.append(current)
      if isinstance(current, ObservableDict):
        for k, v in current.items():
          if isinstance(v, ObservableCollection):
            the_open.append(v)
      elif isinstance(current, ObservableList):
        for k, v in enumerate(current):
          if isinstance(v, ObservableCollection):
            the_open.append(v)
    
    for current in the_closed:
      observer = lambda a, k, v, current=current: self.on_change(current, a, k, v)
      current.add_observer(observer)

  def on_change(self, config: ObservableCollection, action: ObservableCollection.Action, k: typing.Any, v: typing.Any) -> None:
    address = self.compute_address(config)
    value_address = self.__combine(address, k)
    if isinstance(v, ObservableCollection):
      value_string = json.dumps(v.unwrap())
      if action == ObservableCollection.Action.SET:
        self.install_observer(v)
    else:
      value_string = json.dumps(v)

    if address is None or v is None or not self.queues:
      return

    grpc_action = thalamus_pb2.ObservableChange.Action.Set if action == ObservableCollection.Action.SET else thalamus_pb2.ObservableChange.Action.Delete
    message = thalamus_pb2.ObservableChange(
      address = value_address,
      value = value_string,
      action = grpc_action
    )
    transaction = thalamus_pb2.ObservableTransaction(
      changes = [message]
    )
    for queue in self.queues:
      create_task_with_exc_handling(queue.put(transaction))

  async def observable_bridge_v2(self, stream, context):
    message = thalamus_pb2.ObservableTransaction(changes = [
      thalamus_pb2.ObservableChange(
        address = '',
        value = json.dumps(self.config.unwrap()),
        action = thalamus_pb2.ObservableChange.Action.Set
      )
    ])
    yield message
    queue = asyncio.Queue()

    async def reader():
      try:
        async for transaction in stream:
          for change in transaction.changes:
            value = json.loads(change.value)

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
                match.value.assign(value)
              elif isinstance(match.path, jsonpath_ng.Index):
                if match.path.index == len(match.context.value):
                  match.context.value.append(value)
                else:
                  match.context.value[match.path.index] = value
              elif isinstance(match.path, jsonpath_ng.Fields):
                match.context.value[match.path.fields[0]] = value
            
          await queue.put(thalamus_pb2.ObservableTransaction(acknowledged = transaction.id))
      finally:
        await queue.put(None)

    try:
      self.queues.append(queue)
      reader_task = create_task_with_exc_handling(reader())

      while True:
        value = await queue.get()
        if value is None:
          break
        elif value is STOP:
          reader_task.cancel()
        else:
          yield value

      try:
        await reader_task
      except asyncio.CancelledError:
        pass
    finally:
      for i, q in enumerate(self.queues):
        if q is queue:
          del self.queues[i]
          break

  async def observable_bridge_read(self, request, context):
    yield thalamus_pb2.ObservableTransaction(changes = [
      thalamus_pb2.ObservableChange(
        address = '',
        value = json.dumps(self.config),
        action = thalamus_pb2.ObservableChange.Action.Set
      )
    ])
    queue = asyncio.Queue()
    self.queues.append(queue)
    async with self.condition:
      self.peer_name_to_queue[request.peer_name] = queue
      self.condition.notify_all()
    try:
      while True:
        yield await queue.get()
    finally:
      for i, q in enumerate(self.queues):
        if q is queue:
          del self.queues[i]
          break
      async with self.condition:
        del self.peer_name_to_queue[request.peer_name]
        self.condition.notify_all()

  async def observable_bridge_write(self, request, context):
    for change in request.changes:
      value = json.loads(change.value)

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
          match.value.assign(value)
        elif isinstance(match.path, jsonpath_ng.Index):
          if match.path.index == len(match.context.value):
            match.context.value.append(value)
          else:
            match.context.value[match.path.index] = value
        elif isinstance(match.path, jsonpath_ng.Fields):
          match.context.value[match.path.fields[0]] = value

    async with self.condition:
      self.condition.wait(lambda: request.peer_name in self.peer_name_to_queue)
      queue = self.peer_name_to_queue[request.peer_name]
      await queue.put(thalamus_pb2.ObservableTransaction(acknowledged = request.id))

