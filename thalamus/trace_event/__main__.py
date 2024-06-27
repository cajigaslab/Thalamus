import time
import typing
import random
import logging
import asyncio
import argparse
import datetime

import pypesaran.trace_event

import grpc
                                                                                
from . import trace_event_grpc_pb2                                         
from . import trace_event_grpc_pb2_grpc

UNHANDLED_EXCEPTION: typing.List[Exception] = []

LOGGER = logging.getLogger(__name__)

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
                                     
class TraceEventServicer(trace_event_grpc_pb2_grpc.TraceEventServicer):
  def __init__(self):
    super().__init__()
    self.used_pids = set()

  async def trace(self, request_iterator: typing.AsyncIterable[trace_event_grpc_pb2.TraceEventRequest], context):
    local_pids = set()
    pid_remaps = {}
    async for request in request_iterator:
      args = {}
      for arg in request.args:
        if arg.type == trace_event_grpc_pb2.TraceEventArg.Type.DOUBLE:
          value = arg.a_double
        elif arg.type == trace_event_grpc_pb2.TraceEventArg.Type.INT32:
          value = arg.a_int32
        elif arg.type == trace_event_grpc_pb2.TraceEventArg.Type.UINT32:
          value = arg.a_uint32
        elif arg.type == trace_event_grpc_pb2.TraceEventArg.Type.INT64:
          value = arg.a_int64
        elif arg.type == trace_event_grpc_pb2.TraceEventArg.Type.UINT64:
          value = arg.a_uint64
        elif arg.type == trace_event_grpc_pb2.TraceEventArg.Type.BOOL:
          value = arg.a_bool
        elif arg.type == trace_event_grpc_pb2.TraceEventArg.Type.STRING:
          value = arg.a_string
        arg[arg.key] = value

      pid = pid_remaps.get(request.pid, request.pid)
      if pid in self.used_pids and pid not in local_pids:
        old_pid = pid
        while pid in self.used_pids:
          pid = random.randint(0, 2**31)
        pid_remaps[old_pid] = pid

      self.used_pids.add(pid)
      local_pids.add(pid)

      if request.ph in ('b', 'e'):
        event = pypesaran.trace_event.AsyncEvent(
          request.name,
          request.cat,
          request.ph,
          time.perf_counter()*1e6,
          pid,
          request.tid,
          request.id,
          args)
      else:
        event = pypesaran.trace_event.Event(
          request.name,
          request.cat,
          request.ph,
          time.perf_counter()*1e6,
          pid,
          request.tid,
          args)

      with pypesaran.trace_event.Globals.LOCK:
        if event.ph == 'M':
          pypesaran.trace_event.Globals.METADATA_EVENTS.append(event)
        else:
          pypesaran.trace_event.Globals.TRACE_EVENTS.append(event)

      return trace_event_grpc_pb2.TraceEventResponse()

async def async_main():
  asyncio.get_event_loop().set_exception_handler(exception_handler)
  logging.basicConfig(level=logging.DEBUG, format='%(levelname)s %(asctime)s %(name)s:%(lineno)s %(message)s',)
  
  parser = argparse.ArgumentParser(description='trace_event server')
  parser.add_argument('-p', '--port', default=50051, help='Server port')
  parser.add_argument('-i', '--interval', default=20, help='Interval to generate trace files')
  parser.add_argument('-d', '--directory', default='trace_event_data', help='Directory to write trace files to')
  arguments = parser.parse_args()

  pypesaran.trace_event.start(datetime.timedelta(seconds=arguments.interval), arguments.directory)

  server = grpc.aio.server()
  trace_event_grpc_pb2_grpc.add_TraceEventServicer_to_server(TraceEventServicer(), server)
  listen_addr = f'[::]:{arguments.port}'

  server.add_insecure_port(listen_addr)
  LOGGER.info("Starting server on %s", listen_addr)
  await server.start()
  await server.wait_for_termination()

  pypesaran.trace_event.stop()

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
  main()