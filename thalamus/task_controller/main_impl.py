"""
Entrypoing
"""

import sys
import typing
import asyncio
import logging
import pathlib
import argparse
import itertools

import yaml

import PyQt5.QtWidgets

from . import task_context
from . import tasks
from . import window
from ..config import *

from pkg_resources import resource_string, resource_filename

from .controller import ControlWindow, ConfigData

import grpc
from .. import task_controller_pb2_grpc
from .. import ophanim_pb2_grpc
from .. import thalamus_pb2_grpc
from .servicer import TaskControllerServicer
from .observable_bridge import ObservableBridge
from ..thalamus_window import ThalamusWindow

UNHANDLED_EXCEPTION: typing.List[Exception] = []

def exception_handler(loop: asyncio.AbstractEventLoop, context: typing.Mapping[str, typing.Any]) -> None:
  """
  Logs unhandled exceptions and terminates program
  """
  logging.error(context['message'])

  if 'exception' not in context:
    return

  logging.exception('', exc_info=context['exception'])
  UNHANDLED_EXCEPTION.append(context['exception'])
  loop.stop()

def parse_args() -> argparse.Namespace:
  '''
  Parse command line arguments
  '''
  try:
    self_args = list(itertools.takewhile(lambda a: a != '--ros-args', sys.argv))
  except ValueError:
    self_args = list(sys.argv)
  try:
    i = self_args.index('-platform')
    del self_args[i:i+2]
  except ValueError:
    pass

  parser = argparse.ArgumentParser(description='Touch Task ROS node')
  parser.add_argument('-c', '--config', help='Config file location')
  parser.add_argument('-p', '--port', help='Config file location')
  parser.add_argument('-e', '--recorder-url', help='Recorder URL')
  parser.add_argument('-o', '--ophanim-url', help='Ophanim URL')
  parser.add_argument('-t', '--trace', action='store_true', help='Enable tracing')
  parser.add_argument('-r', '--remote-executor', action='store_true',
                      help='Send task configs to remote ROS node to execute')
  return parser.parse_args(self_args[1:])

async def async_main() -> None:
  """
  Entrypoint
  """
  done_future = asyncio.get_event_loop().create_future()

  asyncio.get_event_loop().set_exception_handler(exception_handler)
  logging.basicConfig(level=logging.DEBUG, format='%(levelname)s %(asctime)s %(name)s:%(lineno)s %(message)s')
  logging.getLogger('matplotlib.font_manager').setLevel(logging.INFO)

  arguments = parse_args()

  _ = PyQt5.QtWidgets.QApplication(sys.argv)

  if arguments.config:
    config = load(arguments.config)
  else:
    config = ObservableDict({
      'touch_channels': [0, 1], 
      'task_clusters': [], 
      'queue': [], 
      'reward_schedule': {'schedules': [[0]], 'index': 0},
    })
  if 'nodes' not in config:
    config['nodes'] = []
  for node in config['nodes']:
    if 'Running' in node:
      node['Running'] = False
  
  bmbi_native_filename = resource_filename('bmbi', 'bmbi_native' + ('.exe' if sys.platform == 'win32' else ''))
  bmbi_native_proc = None
  bmbi_native_proc = await asyncio.create_subprocess_exec(
        bmbi_native_filename, 'thalamus', '--slave', *(['--trace'] if arguments.trace else []))

  channel = grpc.aio.insecure_channel('localhost:50050')
  await channel.channel_ready()
  stub = thalamus_pb2_grpc.ThalamusStub(channel)
  observable_bridge = ObservableBridge(stub, config)

  user_config_path = pathlib.Path.home().joinpath('.task_controller', 'config.yaml')
  if user_config_path.exists():
    with open(str(user_config_path)) as user_config_file:
      user_config = yaml.load(user_config_file, Loader=yaml.FullLoader)
      config.merge(user_config)
  else:
    user_config = {}

  screen_geometry = PyQt5.QtWidgets.QApplication.desktop().screenGeometry()

  server = grpc.aio.server()
  servicer = TaskControllerServicer()
  task_controller_pb2_grpc.add_TaskControllerServicer_to_server(servicer, server)
  listen_addr = f'[::]:50051'

  server.add_insecure_port(listen_addr)
  logging.info("Starting GRPC server on %s", listen_addr)
  await server.start()

  if arguments.remote_executor:
    window = None
  else:
    if arguments.ophanim_url:
      ophanim_channel = grpc.insecure_channel(arguments.ophanim_url)
      print('Waiting for ophanim')
      grpc.channel_ready_future(ophanim_channel).result()
      ophanim_stub = ophanim_pb2_grpc.OphanimStub(ophanim_channel)
    else:
      ophanim_stub = None

    if arguments.recorder_url:
      recorder_channel = grpc.insecure_channel(arguments.recorder_url)
      print('Waiting for recorder')
      grpc.channel_ready_future(recorder_channel).result()
      recorder_stub = recorder2_pb2_grpc.RecorderStub(recorder_channel)
    else:
      recorder_stub = None

    window = window.Window(config, done_future, recorder_stub, ophanim_stub, arguments.port)
    #node.create_timer(1/60, PyQt5.QtWidgets.QApplication.processEvents)

    window.resize(1024, 768)

    window.move(
      (screen_geometry.width()-window.width()) // 2,
      (screen_geometry.height()-window.height()) // 2)
    window.setWindowTitle('Touch Task')
    window.show()

  task_context = task_context.TaskContext(config,
                                          window.get_canvas() if window else None,
                                          tasks.DESCRIPTIONS_MAP, servicer)
  servicer.task_context = task_context
  task_context.start()
  window.set_task_context(task_context)

  controller = ControlWindow(window, task_context, ConfigData(user_config, arguments.config), done_future)
  controller.resize(1024, 768)
  controller.move(
    (screen_geometry.width()-controller.width()) // 2 + 50,
    (screen_geometry.height()-controller.height()) // 2 + 50)
  controller.show()

  thalamus = ThalamusWindow(config, stub, done_future)
  await thalamus.load()
  thalamus.resize(384, 768)
  thalamus.move(100, 100)
  thalamus.show()

  try:
    while not done_future.done() and not UNHANDLED_EXCEPTION:
      PyQt5.QtWidgets.QApplication.processEvents()
      task_context.process()
      await asyncio.sleep(.016)
      #await asyncio.sleep(.001)
    if not done_future.done():
      done_future.set_result(None)
    stop = task_context.stop()
    task_context.process()
    await task_context.cleanup()
    await stop
  except KeyboardInterrupt:
    pass

  await channel.close()
  if bmbi_native_proc:
    await bmbi_native_proc.wait()

  if servicer is not None:
    servicer.stop()
  print('DONE')

def main() -> None:
  '''
  Setup before running async_main
  '''
  loop = asyncio.get_event_loop()
  try:
    loop.run_until_complete(async_main())
  except RuntimeError:
    if not UNHANDLED_EXCEPTION:
      raise
  finally:
    if UNHANDLED_EXCEPTION:
      raise UNHANDLED_EXCEPTION[0] from None

if __name__ == '__main__':
  #cProfile.run('main()')
  main()
