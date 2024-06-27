"""
Module for performance profiling
"""

import os
import json
import time
import queue
import typing
import datetime
import threading
import contextlib
import multiprocessing

import grpc
                                                                                
#from . import trace_event_grpc_pb2                                         
#from . import trace_event_grpc_pb2_grpc

EventArgs = typing.Mapping[str, typing.Any]

class Event(typing.NamedTuple):
  """
  A trace_event event
  """
  name: str
  cat: str
  ph: str # pylint: disable=invalid-name
  ts: float # pylint: disable=invalid-name
  pid: int
  tid: int
  args: EventArgs

class AsyncEvent(typing.NamedTuple): # pylint: disable=too-many-instance-attributes
  """
  An async trace_event event
  """
  name: str
  cat: str
  ph: str # pylint: disable=invalid-name
  ts: float # pylint: disable=invalid-name
  pid: int
  tid: int
  id: int # pylint: disable=invalid-name
  args: EventArgs

class Local(threading.local):
  def __init__(self):
    super().__init__()
    self.initialized = False

class Globals():
  """
  Globals for this module
  """
  TRACE_QUEUE: queue.Queue = queue.Queue()
  METADATA_EVENTS: typing.List[Event] = []
  LOCAL = Local()

  LOCK = threading.Lock()

  START = 0.0
  now: typing.Callable[[], float] = lambda: 0.0

  ENABLED = False
  INTERVAL = None
  FOLDER_NAME = None
  THREAD = None
  SUBPROC_THREAD = None
  SUBPROC_QUEUE = None
  CHANNEL = None

  @staticmethod
  def serialize_thread(interval: datetime.timedelta, folder_name: str) -> None:
    """
    This is the thread that will periodically clear the contents of TRACE_EVENTS and dump them to a file.
    """

    count = 0

    os.makedirs(folder_name, exist_ok=True)

    meta_events = []

    while Globals.ENABLED:
      with open(os.path.join(folder_name, f'{count}.json'), 'w', encoding='utf-8') as output:
        output.write("{\"traceEvents\":[\n")
        first_event = None
        elapsed = datetime.timedelta(seconds=0)
        while Globals.ENABLED and elapsed < interval:
          try:
            event = Globals.TRACE_QUEUE.get(timeout=1)
          except queue.Empty:
            pass
          if event.ph == 'M':
            meta_events.append(event)
            continue

          if first_event is None:
            first_event = event
          else:
            output.write(',\n')
          json.dump(event._asdict(), output)
          elapsed = datetime.timedelta(microseconds = event.ts - first_event.ts)
        
        for event in meta_events:
          output.write(',\n')
          json.dump(event._asdict(), output)
        output.write('\n]}')
        count += 1

  @staticmethod
  def start(interval: datetime.timedelta, folder_name: str, time_function: typing.Callable[[], float], proc_name: str) -> None:
    """
    Starts the serialization thread and causes all later *_event calls to log tracing data.
    """
    Globals.now = time_function
    Globals.START = Globals.now()
    Globals.ENABLED = True
    Globals.THREAD = threading.Thread(target=Globals.serialize_thread, args=(interval, folder_name))

    args = {'name': proc_name}
    ident = threading.get_ident()
    pid = os.getpid()
    proc_event = Event('process_name', '1', 'M', 0, pid, ident, args)
    Globals.TRACE_QUEUE.put(proc_event)

    Globals.THREAD.start()

  @staticmethod
  def stop() -> None:
    """
    Stops the serialization thread causes all later *_event calls to be noops.
    """
    Globals.ENABLED = False

    if Globals.THREAD:
      Globals.THREAD.join()
    if Globals.SUBPROC_THREAD:
      Globals.SUBPROC_THREAD.join()

  @staticmethod
  def forward(event) -> None:
    if Globals.CHANNEL:
      with Globals.LOCK:
        Globals.CHANNEL.send(event)
    else:
      Globals.TRACE_QUEUE.put(event)

def start(interval: datetime.timedelta, folder_name: str,
          time_function: typing.Callable[[], float] = time.perf_counter,
          proc_name: str = 'main') -> None:
  """
  ALias for Globals.start
  """
  Globals.start(interval, folder_name, time_function, proc_name)

def stop() -> None:
  """
  ALias for Globals.stop
  """
  Globals.stop()

def begin_event(name: str,
                args: typing.Optional[EventArgs] = None, pid: int = 0, ident: int = 0, async_id: int = 0) -> None:
  """
  Logs the start of an event in the tracing data.
  """
  if not Globals.ENABLED:
    return

  if not pid or not ident:
    pid = os.getpid()
    ident = threading.get_ident()

  now = Globals.now() - Globals.START

  event: typing.Union[Event, AsyncEvent]
  if async_id:
    event = AsyncEvent(name, '1', 'b', now * 1e6, pid, ident, async_id, args if args else {})
  else:
    event = Event(name, '1', 'B', now * 1e6, pid, ident, args if args else {})

  if not Globals.LOCAL.initialized:
    thread_args = {'name': threading.current_thread().name}
    thread_event = Event('thread_name', '1', 'M', 0, pid, ident, thread_args)
    Globals.forward(thread_event)
    Globals.LOCAL.initialized = True

  Globals.forward(event)

def end_event(name: str,
              args: typing.Optional[EventArgs] = None, pid: int = 0, ident: int = 0, async_id: int = 0) -> None:
  """
  Logs the end of an event in the tracing data.
  """
  if not Globals.ENABLED:
    return

  if not pid or not ident:
    pid = os.getpid()
    ident = threading.get_ident()

  now = Globals.now() - Globals.START

  event: typing.Union[Event, AsyncEvent]
  if async_id:
    event = AsyncEvent(name, '1', 'e', now * 1e6, pid, ident, async_id, args if args else {})
  else:
    event = Event(name, '1', 'E', now * 1e6, pid, ident, args if args else {})

  Globals.forward(event)

def instant_event(name: str,
                  args: typing.Optional[EventArgs] = None, pid: int = 0, ident: int = 0) -> None:
  """
  Logs the end of an instantaneous event in the tracing data.
  """
  if not Globals.ENABLED:
    return

  if not pid or not ident:
    pid = os.getpid()
    ident = threading.get_ident()

  now = Globals.now() - Globals.START
  event = Event(name, '1', 'i', now * 1e6, pid, ident, args if args else {})

  Globals.forward(event)

@contextlib.contextmanager
def context(name: str, raw_args: typing.Optional[EventArgs] = None) -> typing.Iterator[None]:
  """
  Context manager and decorator that calls begin_event upon entering and end_event upon exiting.
  """
  if not Globals.ENABLED:
    yield
    return

  args = raw_args if raw_args else {}
  ident = threading.get_ident()
  pid = os.getpid()

  begin_event(name, args, pid, ident)
  yield
  end_event(name, args, pid, ident)

RETURN = typing.TypeVar('RETURN')

def decorator(func: typing.Callable[..., RETURN]) -> typing.Callable[..., RETURN]:
  """
  Context manager and decorator that calls begin_event upon entering and end_event upon exiting.
  """

  def wrapper(*args: typing.Any, **kwargs: typing.Any) -> RETURN:
    """
    Decorator implemtation
    """
    if Globals.ENABLED:
      with context(func.__qualname__, {}):
        return func(*args, **kwargs)
    else:
      return func(*args, **kwargs)

  return wrapper

def subproc_channel_thread_target():
  while Globals.ENABLED:
    try:
      event = Globals.SUBPROC_QUEUE.get(True, 1)
    except queue.Empty:
      continue
    now = Globals.now() - Globals.START
    patched_event = event._replace(ts=now*1e6)
    Globals.forward(patched_event)

class SubprocChannel():
  def __init__(self, the_queue: multiprocessing.Queue):
    self.queue = the_queue
    self.enabled = the_queue is not None

  def send(self, event) -> None:
    self.queue.put(event)

def set_channel(channel: SubprocChannel, name: str) -> None:
  Globals.CHANNEL = channel
  Globals.ENABLED = channel.enabled

  if Globals.ENABLED:
    args = {'name': name}
    ident = threading.get_ident()
    pid = os.getpid()
    proc_event = Event('process_name', '1', 'M', 0, pid, ident, args)
    Globals.forward(proc_event)

def create_subproc_channel():
  if not Globals.ENABLED:
    return SubprocChannel(None)

  if not Globals.SUBPROC_THREAD:
    Globals.SUBPROC_QUEUE = multiprocessing.Queue()
    Globals.SUBPROC_THREAD = threading.Thread(target=subproc_channel_thread_target)
    Globals.SUBPROC_THREAD.start()

  return SubprocChannel(Globals.SUBPROC_QUEUE)

#class RemoteChannel():
#  def __init__(self, stub: trace_event_grpc_pb2_grpc.TraceEventStub) -> None:
#    self.stub = stub
#    self.lock = threading.Lock()
#    self.condition = threading.Condition()
#    self.events = []
#    self.trace_future = self.stub.trace.future(self.requests())
#
#  def requests(self):
#    with self.condition:
#      while True:
#        for event in self.events:
#          message = trace_event_grpc_pb2.TraceEventRequest()
#          message.name = event.name
#          message.cat = event.cat
#          message.ph = event.ph
#          message.ts = event.ts
#          message.pid = event.pid
#          message.tid = event.tid
#          if hasattr(event, 'id'):
#            message.id = event.id
#          for key, value in event.args.items():
#            if isinstance(value, float):
#              message.args.append(trace_event_grpc_pb2.TraceEventArg(key=key,a_double=value))
#            elif isinstance(value, int):
#              message.args.append(trace_event_grpc_pb2.TraceEventArg(key=key,a_int64=value))
#            elif isinstance(value, bool):
#              message.args.append(trace_event_grpc_pb2.TraceEventArg(key=key,a_bool=value))
#            elif isinstance(value, str):
#              message.args.append(trace_event_grpc_pb2.TraceEventArg(key=key,a_string=value))
#          yield message
#        self.events = []
#        self.condition.wait()
#
#  def send(self, event) -> None:
#    with self.condition:
#      self.events.append(event)
#      self.condition.notify()
#
#def set_remote_channel(address: str, name: str):
#  channel = grpc.insecure_channel(address)
#  stub = trace_event_grpc_pb2_grpc.TraceEventStub(channel)
#  trace_channel = RemoteChannel(stub)
#  set_channel(trace_channel, name)
