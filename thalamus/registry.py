import sys
import grpc
import json
import numpy
import typing
import asyncio
import logging
import argparse
import datetime
import traceback
import functools
import pprint

from .thread import ThalamusThread
from .util import MeteredUpdater, IterableQueue
from .task_controller.util import create_task_with_exc_handling
from .config import ObservableDict

from . import  thalamus_pb2
from . import thalamus_pb2_grpc

from .qt import *
import jsonpath_ng

from .config import ObservableCollection

class MatchContext(typing.NamedTuple):
  value: ObservableCollection

class PatchMatch(typing.NamedTuple):
  value: typing.Optional[ObservableCollection]
  context: MatchContext
  path: jsonpath_ng.Child

LOGGER = logging.getLogger(__name__)

async def main():
  parser = argparse.ArgumentParser(description='Thalamus Image Viewer')
  parser.add_argument('-a', '--address', default='localhost:50050', help='Thalamus addres, [ip:port]')
  parser.add_argument('-p', '--path', help='JSONPath expression')
  parser.add_argument('-s', '--set', help='Value to assign')
  parser.add_argument('-d', '--delete', action='store_true', help='Value to assign')
  args = parser.parse_args()

  _ = QApplication(sys.argv)

  thread = ThalamusThread(args.address)
  task = await thread.async_start()
  
  print('Path:', args.path)

  try:
    jsonpath_expr = jsonpath_ng.parse(args.path)
  except Exception as _exc: # pylint: disable=broad-except
    LOGGER.exception('Failed to parse JSONPATH %s', args.path)
    return
  
  #pprint.pprint(thread.config.unwrap())
  matches = jsonpath_expr.find(thread.config)

  if not matches:
    if isinstance(jsonpath_expr, jsonpath_ng.Child):
      for m in jsonpath_expr.left.find(thread.config):
        matches.append(PatchMatch(None, MatchContext(m.value), jsonpath_expr.right))
    else:
      matches.append(PatchMatch(None, MatchContext(thread.config), jsonpath_expr))

  try:
    value = json.loads(args.set) if args.set is not None else None
  except json.JSONDecodeError:
    LOGGER.exception('Failed to decode JSON: %s', args.set)
    return
  
  def unwrap(v):
    if isinstance(v, ObservableCollection):
      return v.unwrap()
    return v

  future = asyncio.get_running_loop().create_future()
  for match in matches:
    if isinstance(match.value, ObservableCollection):
      if args.delete:
        if match.value.parent is not None:
          key = match.value.key_in_parent()
          match.value.parent.delitem(key, lambda: future.set_result(None))
      elif value is not None:
        match.value.assign(value, lambda: future.set_result(None))
      else:
        pprint.pprint(unwrap(match.value))
        future.set_result(None)

    elif isinstance(match.path, jsonpath_ng.Index):
      if args.delete:
        match.context.value.delitem(match.path.index, lambda: future.set_result(None))
      elif value is not None:
        if match.path.index == len(match.context.value):
          match.context.value.append(value, lambda: future.set_result(None))
        else:
          match.context.value.setitem(match.path.index, value, lambda: future.set_result(None))
      else:
        pprint.pprint(unwrap(match.context.value[match.path.index]))
        future.set_result(None)
    elif isinstance(match.path, jsonpath_ng.Fields):
      if args.delete:
        match.context.value.delitem(match.path.fields[0], lambda: future.set_result(None))
      elif value is not None:
        match.context.value.setitem(match.path.fields[0], value, lambda: future.set_result(None))
      else:
        pprint.pprint(unwrap(match.context.value[match.path.fields[0]]))
        future.set_result(None)

  await future
  task.cancel()
  try:
    await task
  except asyncio.CancelledError:
    pass

asyncio.run(main())