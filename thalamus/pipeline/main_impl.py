"""
Entrypoing
"""

import pathlib

#If proto/thalamus.proto exists (implies we are in repo checkout) then regenerate protobuf interfaces if they are out of date
from ..build import generate
THALAMUS_PROTO_PATH = pathlib.Path.cwd() / 'proto' / 'thalamus.proto'
THALAMUS_PROTO_GEN_PATH = pathlib.Path.cwd() / 'thalamus' / 'thalamus_pb2.py'
if THALAMUS_PROTO_PATH.exists() and not THALAMUS_PROTO_GEN_PATH.exists() or (THALAMUS_PROTO_PATH.exists() and THALAMUS_PROTO_GEN_PATH.exists() and THALAMUS_PROTO_GEN_PATH.stat().st_mtime < THALAMUS_PROTO_PATH.stat().st_mtime):
  generate()

import os
import sys
import typing
import asyncio
import logging
import argparse
import itertools

import yaml

from ..config import *

from pkg_resources import resource_string, resource_filename

import grpc
from .. import ophanim_pb2_grpc
from .. import thalamus_pb2_grpc
from ..task_controller.observable_bridge import ObservableBridge
from .thalamus_window import ThalamusWindow
from ..servicer import ThalamusServicer

from ..qt import *
from .. import process
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

  parser = argparse.ArgumentParser(description='Thalamus signal pipeline')
  parser.add_argument('-c', '--config', help='Config file location')
  parser.add_argument('-t', '--trace', action='store_true', help='Enable tracing')
  parser.add_argument('-p', '--port', type=int, default=50050, help='GRPC port')
  parser.add_argument('-u', '--ui-port', type=int, default=50051, help='UI GRPC port')
  return parser.parse_args(self_args[1:])

async def async_main() -> None:
  """
  Entrypoint
  """
  
  if 'QT_QPA_PLATFORM_PLUGIN_PATH' in os.environ:
    del os.environ['QT_QPA_PLATFORM_PLUGIN_PATH']

  done_future = asyncio.get_event_loop().create_future()

  asyncio.get_event_loop().set_exception_handler(exception_handler)
  logging.basicConfig(level=logging.DEBUG, format='%(levelname)s %(asctime)s %(name)s:%(lineno)s %(message)s')
  logging.getLogger('matplotlib.font_manager').setLevel(logging.INFO)

  arguments = parse_args()

  _ = QApplication(sys.argv)

  if arguments.config:
    config = load(arguments.config)
  else:
    config = ObservableDict({})
  if 'nodes' not in config:
    config['nodes'] = []
  for node in config['nodes']:
    if 'Running' in node:
      node['Running'] = False

  server = grpc.aio.server()
  servicer = ThalamusServicer(config)
  thalamus_pb2_grpc.add_ThalamusServicer_to_server(servicer, server)
  listen_addr = f'[::]:{arguments.ui_port}'

  server.add_insecure_port(listen_addr)
  logging.info("Starting GRPC server on %s", listen_addr)
  await server.start()
  
  bmbi_native_filename = resource_filename('thalamus', 'native' + ('.exe' if sys.platform == 'win32' else ''))
  bmbi_native_proc = None
  bmbi_native_proc = await asyncio.create_subprocess_exec(
      bmbi_native_filename, 'thalamus', '--port', str(arguments.port), '--state-url', f'localhost:{arguments.ui_port}', *(['--trace'] if arguments.trace else []))

  channel = grpc.aio.insecure_channel(f'localhost:{arguments.port}')
  await channel.channel_ready()
  stub = thalamus_pb2_grpc.ThalamusStub(channel)

  screen_geometry = qt_screen_geometry()

  thalamus = ThalamusWindow(f'localhost:{arguments.port}', config, stub, done_future)
  thalamus.enable_config_menu(arguments.config)
  await thalamus.load()
  thalamus.show()

  try:
    while not done_future.done() and not UNHANDLED_EXCEPTION:
      QApplication.processEvents()
      await asyncio.sleep(.016)
    if not done_future.done():
      done_future.set_result(None)
  except KeyboardInterrupt:
    pass

  await servicer.stop()
  await channel.close()
  if bmbi_native_proc:
    await bmbi_native_proc.wait()

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
    process.cleanup()
    if UNHANDLED_EXCEPTION:
      raise UNHANDLED_EXCEPTION[0] from None

if __name__ == '__main__':
  #cProfile.run('main()')
  main()
