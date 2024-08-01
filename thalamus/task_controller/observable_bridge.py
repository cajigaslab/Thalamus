import json
import time
import typing
import logging
import asyncio

from ..qt import *
from ..config import ObservableCollection, ObservableDict, ObservableList
from .. import util_pb2
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from ..util import IterableQueue
from .util import create_task_with_exc_handling

import jsonpath_ng

LOGGER = logging.getLogger(__name__)

class MatchContext(typing.NamedTuple):
  value: ObservableCollection

class PatchMatch(typing.NamedTuple):
  value: typing.Optional[ObservableCollection]
  context: MatchContext
  path: jsonpath_ng.Child

class ObservableBridge:

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

  def __init__(self, stub: thalamus_pb2_grpc.ThalamusStub, config: ObservableCollection):
    self.stub = stub
    self.config = config
    self.queue = IterableQueue()
    self.eval_queue = IterableQueue()
    self.task = create_task_with_exc_handling(self.__bridge_processor())
    self.eval_task = create_task_with_exc_handling(self.__eval_processor())
    self.notification_task = create_task_with_exc_handling(self.__notification_processor())

    self.install_observer(config)

    message = thalamus_pb2.ObservableChange(
      address = '',
      value = json.dumps(config.unwrap()),
      action = thalamus_pb2.ObservableChange.Action.Set
    )
    #print(message)
    create_task_with_exc_handling(self.queue.put(message))

  async def __notification_processor(self):
    async for request in self.stub.notification(util_pb2.Empty()):
      func = None
      if request.type == thalamus_pb2.Notification.Type.Info:
        func = QMessageBox.information
      elif request.type == thalamus_pb2.Notification.Type.Warning:
        func = QMessageBox.warning
      elif request.type == thalamus_pb2.Notification.Type.Error:
        func = QMessageBox.critical
      assert func is not None, f'Unexpected notification type: {request.type}'
      func(None, request.title, request.message)

  async def __eval_processor(self):
    async for request in self.stub.eval(self.eval_queue):
      root = self.config
      result = eval(request.code)
      json_result = json.dumps(result)
      await self.eval_queue.put(thalamus_pb2.EvalResponse(id = request.id, value = json_result))

  async def __bridge_processor(self):
    async for change in self.stub.observable_bridge(self.queue):
      #LOGGER.info('Change: %s = %s', change.address, change.value)
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
      
      message = thalamus_pb2.ObservableChange(acknowledged = change.id)
      create_task_with_exc_handling(self.queue.put(message))

  def __combine(self, pre: str, end: typing.Union[str, int]) -> None:
    if pre is None:
      if isinstance(end, str):
        return f'[\'{end}\']'
      else:
        return f'[{end}]'
    if isinstance(end, str):
      return f'{pre}[\'{end}\']'
    else:
      return f'{pre}[{end}]'

  def on_change(self, config: ObservableCollection, action: ObservableCollection.Action, k: typing.Any, v: typing.Any) -> None:
    address = self.compute_address(config)
    value_address = self.__combine(address, k)
    if isinstance(v, ObservableCollection):
      value_string = json.dumps(v.unwrap())
      if action == ObservableCollection.Action.SET:
        self.install_observer(v)
    else:
      value_string = json.dumps(v)

    if address is None or v is None:
      return

    grpc_action = thalamus_pb2.ObservableChange.Action.Set if action == ObservableCollection.Action.SET else thalamus_pb2.ObservableChange.Action.Delete
    message = thalamus_pb2.ObservableChange(
      address = value_address,
      value = value_string,
      action = grpc_action
    )
    #print(message)
    create_task_with_exc_handling(self.queue.put(message))
