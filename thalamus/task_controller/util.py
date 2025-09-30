'''
Misc utilities
'''
import abc
import enum
import time
import queue
import struct
import typing
import logging
import asyncio
import datetime
import threading
import contextlib

from ..qt import *

LOGGER = logging.getLogger(__name__)

import stl.mesh

import typing_extensions

from .. import thalamus_pb2

from ..config import ObservableCollection

MESSAGE = typing.TypeVar('MESSAGE')
ParameterType = typing.TypeVar('ParameterType')
                                              

class RenderOutput(enum.Enum):
  '''
  Target for rendering
  '''
  ANY = 1
  SUBJECT = 2
  OPERATOR = 3

class CanvasPainterProtocol(typing_extensions.Protocol):
  '''
  Protocol defining a QPainter for task rendering
  '''
  @contextlib.contextmanager
  def masked(self, mask: RenderOutput) -> typing.Iterator['CanvasPainterProtocol']:
    '''
    Context manager that will disable rendering if we are not rendering to the specified output
    '''

  def render_stl(self, mesh: stl.mesh.Mesh, color: QColor) -> None:
    '''
    Draw an STL mesh
    '''

  @property
  def model_view(self) -> QMatrix4x4:
    '''
    The model view transformation
    '''

  def fillRect(self, *args: typing.Any, **kwargs: typing.Any) -> None: # pylint: disable=invalid-name
    '''
    Override that implements masked rendering
    '''

  def fillPath(self, *args: typing.Any, **kwargs: typing.Any) -> None: # pylint: disable=invalid-name
    '''
    Override that implements masked rendering
    '''

  def drawPath(self, *args: typing.Any, **kwargs: typing.Any) -> None: # pylint: disable=invalid-name
    '''
    Override that implements masked rendering
    '''

class CanvasProtocol(typing_extensions.Protocol):
  """
  The QWidget the task will render on and that will generate mouse events on touch input
  """
  @property
  def renderer(self) -> typing.Callable[[CanvasPainterProtocol], None]:
    '''
    Get renderer callback
    '''

  @renderer.setter
  def renderer(self, value: typing.Callable[[CanvasPainterProtocol], None]) -> None:
    '''
    renderer setter
    '''

  @property
  def touch_listener(self) -> typing.Callable[[QPoint], None]:
    '''
    Get touch callback
    '''

  @touch_listener.setter
  def touch_listener(self, value: typing.Callable[[QPoint], None]) -> None:
    '''
    touch_listener setter
    '''

  @property
  def gaze_listener(self) -> typing.Callable[[QPoint], None]:
    '''
    Get gaze callback
    '''

  @gaze_listener.setter
  def gaze_listener(self, value: typing.Callable[[QPoint], None]) -> None:
    '''
    gaze_listener setter
    '''

  @contextlib.contextmanager
  def masked(self, mask: RenderOutput) -> typing.Iterator['CanvasProtocol']:
    '''
    Context manager that will disable rendering if we are not rendering to the specified output
    '''

  def calibrate_touch(self) -> None:
    '''
    Initiate touch calibration
    '''

  def update(self) -> None:
    '''
    Schedule widget update
    '''

class TaskResult(typing.NamedTuple):
  '''
  Status of task execution
  '''
  success: bool
  done: bool = True

class TaskContextProtocol(metaclass=abc.ABCMeta):
  """
  Defines the TaskContextProtocol class which is responsible for selecting, running, and providing the tasks an
  interface for rendering and accessing the event loop
  """
  def __init__(self, widget: typing.Optional[CanvasProtocol], config: ObservableCollection):
    self.widget = widget
    self.config = config

  @property
  def behav_result(self) -> typing.Dict[str, typing.Any]:
    '''
    Description of behavior results
    '''

  @behav_result.setter
  def behav_result(self, value: typing.Dict[str, typing.Any]) -> None:
    '''
    Description of behavior results
    '''

  def sleep(self, duration: datetime.timedelta) -> typing.Awaitable[None]:
    """
    Sleeps until duration time has passed
    """

  def until(self, condition: typing.Callable[[], bool]) -> typing.Awaitable[None]:
    """
    Sleeps until condition returns True
    """

  def any(self, *args: typing.Awaitable[None]) -> typing.Awaitable[None]:
    """
    Sleeps until one of the futures finishes at which point the finished future is returned
    """

  def do_yield(self) -> typing.Awaitable[None]:
    """
    Sleeps for one iteration of the event loop
    """

  def subscribe(self, message_type: typing.Type[MESSAGE], topic: str,
                callback: typing.Callable[[MESSAGE], bool]) -> typing.Awaitable[MESSAGE]:
    '''
    Subscribe to a ROS topic
    '''

  def publish(self, message_type: typing.Type[MESSAGE], topic: str, message: MESSAGE) -> None:
    '''
    Publish a ROS message
    '''

  def get_value(self, key: str, default: typing.Any = None) -> typing.Union[int, float, bool]:
    """
    Reads a number from the current task_config.  The specified config value should be a number or an object specifying
    a max and min value.  In the latter case a uniform random number in the range [min, max] will be returned.
    """

  def get_target_value(self, itarg: int, key: str, default: typing.Any = None) -> typing.Union[int, float, bool, str]:
    """
    Reads or samples a number for a specific target in the task_config. The target identity is
    specified by itarg, the index to access the target from task_config['targets'][itarg]. The
    target config value should be a number or an object specifying a min and max value. In the latter
    case, a uniform random number in the range [min, max] will be returned.
    """

  def get_color(self, key: str, default: typing.Optional[typing.List[int]] = None) -> QColor:
    """
    Reads a color from the current task_config.  The specified config value should be a list of numpers specifying RGB
    values.
    """

  def get_target_color(self, itarg: int, key: str,
                       default: typing.Optional[typing.List[int]] = None) -> QColor:
    """
    Reads a color from the current task_config for the target indexed by itarg.  The specified config value should be a
    list of numpers specifying RGB
    values.
    """

  def get_ros_parameter(self, key: str, parameter_type: typing.Type[ParameterType]) -> ParameterType:
    """
    Access a ROS2 parameter
    """

  @property
  def widget(self) -> typing.Optional[CanvasProtocol]:
    """
    Gets the Canvas the task will render on
    """
    return self._widget

  @widget.setter
  def widget(self, widget: CanvasProtocol) -> None:
    """
    Gets the Canvas the task will render on
    """
    self._widget = widget

  def process(self) -> None:
    '''
    Progress task execution
    '''

  def start(self) -> None:
    '''
    Progress task execution
    '''

  def stop(self) -> 'asyncio.tasks.Task[typing.Any]':
    '''
    Progress task execution
    '''

  async def log(self, text: str) -> None:
    '''
    Send log to thalamus
    '''

AnimateTarget = typing.Callable[[TaskContextProtocol],
                                typing.Awaitable[TaskResult]]

def assert_behav_result_has(fields):
  def decorator(func: AnimateTarget) -> AnimateTarget:
    async def wrapper(context: TaskContextProtocol) -> TaskResult:
      result = await func(context)
      for f in fields:
        if f not in context.behav_result:
          QMessageBox.critical(context.widget,
                                              'behav_result Assertion Failed',
                                              f'{f} missing from behav_result')
      return result
    return wrapper
  return decorator

def remove_by_is(collection: typing.Any, value: typing.Any) -> None:
  """
  Reimplementation of list.remove that uses identity instead of value in comparisons
  """
  for i, j in enumerate(collection):
    if j is value:
      del collection[i]
      return
  #pdb.set_trace()
  raise ValueError()

RETURN = typing.TypeVar('RETURN')

def create_task_with_exc_handling(awaitable: 'typing.Awaitable[RETURN]') -> 'asyncio.Task[RETURN]':
  '''
  Wraps the specified awaitable in a task that will call the exception handler on an unhandled exception.
  '''
  async def inner() -> RETURN:
    try:
      return await awaitable
    except Exception as exc: #pylint: disable=broad-except
      if not isinstance(exc, IgnorableError):
        asyncio.get_event_loop().call_exception_handler({
          'message': str(exc),
          'exception': exc
        })
      raise

  return asyncio.get_event_loop().create_task(inner())

def lower_left_origin_transform(height: int) -> QTransform:
  '''
  Returns a QTransform that sets the origin to the lower left corner of a QWidget height pixels tall
  '''
  transform = QTransform()
  transform.translate(0, height)
  transform.scale(1, -1)
  return transform

WithTransformTarget = typing.Union[typing.Callable[[QPoint], None],
                                   typing.Callable[[QPainter], None]]
WithTransformOutput = typing.Callable[[typing.Union[QPoint, QPainter]], None]

def with_transform(transform: QTransform) -> typing.Callable[[WithTransformTarget], WithTransformOutput]:
  '''
  Wraps task renderers or input handlers so that input has the specified transform is applied to the input or QPainter.
  '''
  def decorator(func: WithTransformTarget) -> WithTransformOutput:
    def wrapper(arg: typing.Union[QPoint, QPainter]) -> None:
      if isinstance(arg, QPoint):
        new_point = transform.map(arg)
        typing.cast(typing.Callable[[QPoint], None], func)(new_point)
      else:
        old_transform = arg.transform()
        try:
          arg.setTransform(transform)
          typing.cast(typing.Callable[[QPainter], None], func)(arg)
        finally:
          arg.setTransform(old_transform)
    return wrapper

  return decorator

async def wait_for(context: TaskContextProtocol, condition: typing.Callable[[], bool],
                   timeout: datetime.timedelta) -> bool:
  '''
  Waits for either condition to return true or for the timeout to expire and returns the current value of condition
  '''
  condition_future = context.until(condition)
  timeout_future = context.sleep(timeout)
  temp = await context.any(condition_future, timeout_future)
  return condition()

async def wait_for_dual_hold(context: TaskContextProtocol,
                            hold_duration: datetime.timedelta,
                            is_held1: typing.Callable[[], bool], 
                            is_held2: typing.Callable[[], bool], 
                            blink1_duration: datetime.timedelta,
                            blink2_duration: datetime.timedelta) -> bool:
  """
  Waits for two conditions to be held for hold_duration. Both is_held1 and is_held2 must both maintain true
  throughout.  up to blink1_duration and blink2_duration.
  """
  start = time.perf_counter()

  time_spent_blinking = 0.0
  while True:
    elapsed_time = datetime.timedelta(seconds=time.perf_counter() - start)
    td_spent_blinking = datetime.timedelta(seconds=time_spent_blinking)

    remaining_time = hold_duration - elapsed_time + td_spent_blinking
    #remaining_time = hold_duration - elapsed_time
    if remaining_time.total_seconds() < 0:
      break 
    blinked = await wait_for(context, lambda: not is_held1() or not is_held2(), remaining_time)

    if not blinked:
      break

    await context.log('BehavState=blink')      

    t0 = time.perf_counter()  
    if not is_held1():    
      reacquired = await wait_for(context, is_held1, blink1_duration)
      if not reacquired:
        return False

    if not is_held2():
      reacquired = await wait_for(context, is_held2, blink2_duration)
      if not reacquired:
        return False
    t1 = time.perf_counter()

    time_spent_blinking += t1 - t0

  return True

async def stamp_msg(context, msg):
  msg.header.stamp = context.ros_manager.node.node.get_clock().now().to_msg()
  #context.pulse_digital_channel()
  return msg

async def do_stimulation(context, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period):
  try:
    await context.sleep(stim_start)

    await context.log('BehavState=pulse_start')

    intan_cfg = int(intan_cfg)

    spans = []
    for i in range(32):
      if ((intan_cfg >> i) % 2):
        spans.append(thalamus_pb2.Span(begin=2,end=4,name=str(i)))
      else:
        spans.append(thalamus_pb2.Span(begin=0,end=2,name=str(i)))

    if pulse_width.total_seconds() > 0:
      for _ in range(pulse_count):
        signal = thalamus_pb2.AnalogResponse(
            data=[0,0,5,0],
            spans=spans,
            sample_intervals=[int(1e9*pulse_width.total_seconds())])

        await context.inject_analog('Stim', signal)
    else:
      for _ in range(pulse_count):
        signal = thalamus_pb2.AnalogResponse(
            data=[0,0,5,0],
            spans=spans,
            sample_intervals=[int(1e6)])
        await context.sleep(pulse_period)
  except asyncio.CancelledError:
    pass
  finally:
    await context.log('BehavState=pulse_end')

@contextlib.contextmanager
def stimulator(*args, **kwargs):
  task = create_task_with_exc_handling(do_stimulation(*args, **kwargs))
  try:
    yield
  finally:
    task.cancel()

@contextlib.contextmanager
def nullcontext():
  yield


async def wait_for_hold(context: TaskContextProtocol,
                        is_held: typing.Callable[[], bool],
                        hold_duration: datetime.timedelta,
                        blink_duration: datetime.timedelta) -> bool:
  """
  Waits for the target to be held for hold_duration.  Subject is allowed to blink no longer than blink_duration
  """
  start = time.perf_counter()
  time_spent_blinking = 0.0
  while True:
    elapsed_time = datetime.timedelta(seconds=time.perf_counter() - start)
    td_spent_blinking = datetime.timedelta(seconds=time_spent_blinking)
    
    remaining_time = hold_duration - elapsed_time + td_spent_blinking
    #remaining_time = hold_duration - elapsed_time
    if remaining_time.total_seconds() < 0:
      break 
    blinked = await wait_for(context, lambda: not is_held(), remaining_time)

    if not blinked:
      break
    
    await context.log('BehavState=blink')

    t0 = time.perf_counter()        
    reacquired = await wait_for(context, is_held, blink_duration)
    if not reacquired:
      return False
    t1 = time.perf_counter()
    time_spent_blinking += t1 - t0

  return True

def animate(frequency: int) -> typing.Callable[[AnimateTarget], AnimateTarget]:
  '''
  Creates a decorator for a task implementation that causes the widget to update `frequency` times a second
  '''
  async def animator(context: TaskContextProtocol) -> None:
    assert context.widget, 'context.widget is None'
    while True:
      await context.sleep(datetime.timedelta(seconds=1/frequency))
      context.widget.update()

  def decorator(func: AnimateTarget) -> AnimateTarget:
    async def wrapper(context: TaskContextProtocol) -> TaskResult:
      animate_task = asyncio.get_event_loop().create_task(animator(context))
      try:
        return await func(context)
      finally:
        animate_task.cancel()

    return wrapper

  return decorator

class IgnorableError(Exception):
  '''
  Exception that will not be forwarded to exception handler
  '''

class UdpProtocol:
  def __init__(self, task_context: TaskContextProtocol):
    self.task_context = task_context
    self.callback = lambda message: None
    self.transport = None
    self.is_closed = asyncio.get_running_loop().create_future()

  def connection_made(self, transport):
    self.transport = transport
    LOGGER.info('connection_made')

  def datagram_received(self, data, addr):
    LOGGER.info('datagram_received')
    self.callback(data)
    self.task_context.process()

  def error_received(self, exc):
    LOGGER.info('error_received %s', exc)

  def connection_lost(self, exc):
    LOGGER.info('connection_lost')
    self.is_closed.set_result(None)

class udp_context():
  def __init__(self, task_context: TaskContextProtocol, port: int) -> None:
    self.task_context = task_context
    self.port = port
    self.transport = None
    self.protocol = protocol

  async def __aenter__(self):
    self.transport, self.protocol = await asyncio.get_running_loop().create_datagram_endpoint(
      lambda: UdpProtocol(task_context), local_addr=('127.0.0.1', port))
    return self.protocol

  async def __aexit__(self, exec_type, exec, tb):
    transport.close()
    await protocol.is_closed

class movella_context(udp_context):
  def __init__(self, task_context: TaskContextProtocol, port: int) -> None:
    super().__init__(task_context, port)

  async def __aenter__(self):
    super().__aenter__()
    return MovellaReceiver(self.protocol)

  async def __aexit__(self, exec_type, exec, tb):
    return super().__aexit_(exec_type, exec, tb)

def udp_decorator(task_context: TaskContextProtocol, port: int):
  def decorator(func):
    async def wrapper(*args, **kwargs) -> TaskResult:
      async with udp_context(task_context, port) as protocol:
        try:
          return await func(*(args + (protocol,)), **kwargs)
        finally:
          protocol.transport.close()
          await protocol.is_closed
    
    return wrapper

  return decorator

MVN_HEADER_FORMAT = '>6sIBBIBBBBHH'
MVN_HEADER_SIZE = struct.calcsize(MVN_HEADER_FORMAT)
MVN_SEGMENT_FORMAT = '>Ifffffff'
MVN_SEGMENT_SIZE = struct.calcsize(MVN_SEGMENT_FORMAT)

class Segment(typing.NamedTuple):
  id: int
  position: typing.Tuple[float, float, float]
  rotation: typing.Tuple[float, float, float, float]

class MovellaReceiver:
  def __init__(self, udp: UdpProtocol) -> None:
    self.udp = udp
    self.callback = lambda segments: None
    udp.callback = self.__on_message

  def __on_message(self, message):
    (id_string, sample_counter, datagram_counter, item_num, time_code, character, num_body_segments, num_props,
     num_finger_segments, reserved, payload_size) = struct.unpack(MVN_HEADER_FORMAT, message[:MVN_HEADER_SIZE])
    if id_string[-2:] == b'02':
      payload = message[MVN_HEADER_SIZE:]
      segments = []
      while payload:
        segment_id, x, y, z, r, i, j, k = struct.unpack(MVN_SEGMENT_FORMAT, payload[:MVN_SEGMENT_SIZE])
        segment = Segment(segment_id, (x, y, z), (r, i, j, k))
        segments.append(segment)
        payload = payload[MVN_SEGMENT_SIZE:]
      self.callback(segments)
    else:
      LOGGER.error('MOVELLA: Unsupported ID string, %s', id_string.decode())

def movella_decorator(port: int):
  def decorator(func):
    async def wrapper(task_context: TaskContextProtocol, *args, **kwargs) -> TaskResult:
      async with movella_context(task_context, port) as protocol:
        return await func(task_context, *(args + (protocol,)), **kwargs)
    
    return wrapper

  return decorator
  
