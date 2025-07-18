"""
Defines the TaskContext class which is responsible for selecting, running, and providing the tasks an interface for
rendering and accessing the event loop
"""

import re
import json
import typing
import random
import logging
import asyncio
import datetime
import functools
import collections
import os
import time
import threading
import grpc
try:
  import contextvars
  HAS_CONTEXTVARS = True
except ImportError:
  HAS_CONTEXTVARS = False


try:    
  import comedi.comedi as c
  use_comedi = True
except ImportError:
  use_comedi = False

import jsonpath_ng.ext # type: ignore
import jsonpath_ng.ext.parser # type: ignore

from .. import task_controller_pb2
from .. import task_controller_pb2_grpc
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from .. import util_pb2

from ..qt import *
from ..util import IterableQueue

from .canvas import Canvas
from ..config import ObservableCollection, ObservableDict
from .util import create_task_with_exc_handling, TaskContextProtocol, TaskResult
from .servicer import TaskControllerServicer, ExecutorLostError

LOGGER = logging.getLogger(__name__)

class TaskDescription(typing.NamedTuple):
  '''
  Description of a task
  '''
  code: str
  display_name: str
  create_widget: typing.Callable[[ObservableCollection], QWidget]
  run: typing.Callable[['TaskContextProtocol'], typing.Awaitable[TaskResult]]

async def to_thread(func):
  loop = asyncio.get_event_loop()
  ctx = contextvars.copy_context()
  func_call = functools.partial(ctx.run, func)
  return await loop.run_in_executor(None, func_call)

class Sleeper():
  """
  Provides implementations for waiting for certain conditions
  """
  def __init__(self) -> None:
    self.tasks: typing.List[asyncio.Task[typing.Any]] = []
    self.conditions: typing.List[typing.Tuple[typing.Callable[[], bool], 'asyncio.Future[typing.Any]']] = []
    self.cancelled_futures: typing.List['asyncio.Future[typing.Any]'] = []
    self.running = True
    self.asyncio_sleep: typing.Callable[[float], 'asyncio.Future[None]'] = asyncio.sleep
    self.run_future = asyncio.get_event_loop().create_future()

  def sleep(self, duration: datetime.timedelta) -> 'typing.Awaitable[None]':
    """
    Sleeps until duration time has passed
    """
    #pdb.set_trace()
    if not HAS_CONTEXTVARS:
      return self.sleep_simple(duration)

    condition = threading.Condition()
    def inner() -> None:
      with condition:
        condition.wait(duration.total_seconds())

    def on_done(f: asyncio.Future):
      if f.cancelled():
        with condition:
          condition.notify()
      self.tasks.remove(task)

    task = asyncio.get_event_loop().create_task(to_thread(inner))
    task.add_done_callback(on_done)
    self.tasks.append(task)

    if not self.running:
      task.cancel()

    return task

  def sleep_simple(self, duration: datetime.timedelta) -> 'typing.Awaitable[None]':
    """
    Sleeps until duration time has passed
    """
    #pdb.set_trace()
    async def inner() -> None:
      await self.asyncio_sleep(duration.total_seconds())
      self.tasks.remove(task)

    task = asyncio.get_event_loop().create_task(inner())
    if not self.running:
      task.cancel()
    else:
      self.tasks.append(task)

    return task

  def until(self, condition: typing.Callable[[], bool]) -> 'typing.Awaitable[typing.Any]':
    """
    Sleeps until condition returns True
    """
    future = asyncio.get_event_loop().create_future()
    if not self.running:
      future.set_exception(asyncio.CancelledError())
      return future

    def functor_condition() -> bool:
      """
      This function will be called until the specified condition is True at which point the future is set
      """
      if condition():
        future.set_result(None)
        return True
      return False

    self.conditions.append((functor_condition, future))
    return future

  def any(self, *awaitables: 'typing.Awaitable[typing.Any]') -> 'typing.Awaitable[typing.Any]':
    """
    Sleeps until one of the awaitables finishes at which point the finished future is returned
    """
    async def inner() -> typing.Awaitable[typing.Any]:
      done, _ = await asyncio.wait([asyncio.ensure_future(f) for f in awaitables], return_when=asyncio.FIRST_COMPLETED)
      self.tasks.remove(task)
      return await next(iter(done))

    task = asyncio.get_event_loop().create_task(inner())
    if not self.running:
      task.cancel()
    else:
      self.tasks.append(task)

    return task

  def do_yield(self) -> typing.Awaitable[None]:
    """
    Sleeps for one iteration of the event loop
    """
    future = asyncio.get_event_loop().create_future()
    if not self.running:
      future.set_exception(asyncio.CancelledError())
      return future

    asyncio.get_event_loop().call_soon(lambda: future.set_result(None))
    return future

  async def run(self) -> None:
    """
    Implements the event loop that manages all the futures returned by this class
    """
    self.running = True
    try:
      while self.running:
        self.conditions = [c for c in self.conditions if not c[0]()]
        self.run_future = asyncio.get_event_loop().create_future()
        #await self.asyncio_sleep(1000)
        await self.run_future
    finally:
      self.running = False

  def cancel(self) -> None:
    """
    Cancels all inprogress futures and stops the run loop
    """
    #pdb.set_trace()
    for _, future in self.conditions:
      future.set_exception(asyncio.CancelledError())
      self.cancelled_futures.append(future)
    for task in self.tasks:
      task.cancel()
    self.conditions = []

  def stop(self) -> None:
    """
    Cancels all inprogress futures and stops the run loop
    """
    #pdb.set_trace()
    self.running = False
    for _, future in self.conditions:
      future.set_exception(asyncio.CancelledError())
      self.cancelled_futures.append(future)
    for task in self.tasks:
      task.cancel()
    self.conditions = []

MESSAGE = typing.TypeVar('MESSAGE')
PARAMETERTYPE = typing.TypeVar('PARAMETERTYPE')

class JsonPathParser(jsonpath_ng.ext.parser.ExtentedJsonPathParser): # type: ignore
  '''
  JsonPath extension for task_controller
  '''
  def p_jsonpath_named_operator(self, p: typing.Any) -> jsonpath_ng.jsonpath.JSONPath:
    "jsonpath : NAMED_OPERATOR"
    if p[1].startswith('nth_task('):
      p[0] = self.parse(re.sub(r'nth_task\((\d+)\)', r'queue..`this`[?(queue_index=\1)]', p[1]))
    elif p[1].startswith('task('):
      p[0] = self.parse(re.sub(r'task\((".*"),\s*(".*")\)', r'task_clusters[?(name=\1)].tasks[?(name=\2)]', p[1]))
    else:
      super().p_jsonpath_named_operator(p)

def parse_jsonpath(path: str) -> jsonpath_ng.jsonpath.JSONPath:
  '''
  Parses jsonpath expression using extended parser.
  '''
  return JsonPathParser().parse(path)

def sample_from_cluster(cluster_config: typing.Dict[str, typing.Any]) -> typing.Optional[typing.Any]:
  """
  Selects a task from a task_cluster weighted by the remaining goal for each task
  """
  LOGGER.info('SAMPLING FROM CLUSTER')
  tasks = cluster_config['tasks']
  weights = [t['goal'] for t in tasks]
  if sum(weights) > .5:
    LOGGER.info('%s %s', [t['name'] for t in tasks], weights)
    return random.choices(tasks, weights=weights, k=1)[0]

  return None

def compute_cluster_weight(cluster_config: typing.Dict[str, typing.Any]) -> float:
  tasks = cluster_config['tasks']
  weights = sum(t['goal'] for t in tasks)
  return cluster_config['weight'] if weights > .5 else 0

def sample_from_task(task_config: typing.Dict[str, typing.Any]) -> typing.Optional[typing.Any]:
  """
  When given a task this function will return that task if it has any remaining goal, otherwise, return None.
  """
  LOGGER.info('SAMPLING FROM TASK')
  return task_config if task_config['goal'] else None

class TrialSummaryData:
  '''
  Trial summary fields
  '''
  def __init__(self) -> None:
    self.used_values: typing.Dict[str, typing.Any] = {}
    self.behav_result: typing.Dict[str, typing.Any] = {}
    self.trial_history: typing.Dict[str, typing.Dict[str, int]] = collections.defaultdict(lambda: {'success': 0,
                                                                                                   'failure': 0})
  def __str__(self) -> str:
    return (f'TrialSummaryData(used_values={str(self.used_values)},'
            f'behav_result={str(self.behav_result)},trial_history={str(self.trial_history)})')

  def __repr__(self) -> str:
    return (f'TrialSummaryData(used_values={str(self.used_values)},'
            f'behav_result={str(self.behav_result)},trial_history={str(self.trial_history)})')

class TaskContext(TaskContextProtocol):
  """
  Defines the TaskContext class which is responsible for selecting, running, and providing the tasks an interface for
  rendering and accessing the event loop
  """
  def __init__(self, config: ObservableCollection, widget: typing.Optional[Canvas],
               task_descriptions_map: typing.Dict[str, TaskDescription], servicer: typing.Optional[task_controller_pb2_grpc.TaskControllerServicer],
               stub: thalamus_pb2_grpc.ThalamusStub) -> None:
    super().__init__(widget, config)
    self.sleeper = Sleeper()
    self.task_descriptions_map = task_descriptions_map
    self.running = False
    self.servicer = servicer
    self.stub = stub
    self.log_queue = IterableQueue()
    self.log_stream = stub.log(self.log_queue)
    self.inject_analog_streams: typing.Dict[str, IterableQueue] = {}
    self.stim_streams: typing.Dict[str, IterableQueue] = {}
    self.task_config = ObservableDict({})
    self.channels: typing.Mapping[str, grpc.aio.Channel] = {}
    self.task: asyncio.tasks.Task[typing.Any] = create_task_with_exc_handling(asyncio.sleep(0))
    config['queue'].add_observer(self.__on_queue_changed)
    self.__on_queue_changed(ObservableCollection.Action.SET, None, None)

    # dictionaries to hold key-value information from the currently executing trial.
    self.trial_summary_data = TrialSummaryData()
    config['status'] = ''

    # comedi output
    if use_comedi:
      self.it = c.comedi_open('/dev/comedi0')
      if self.it is None:
        self.use_comedi = False
      else:
        self.use_comedi = True
        print('comedi device name: ')
        print(self.it)
        self.comedi_subd = 2
        self.comedi_ch = 0
        c.comedi_dio_config(self.it, 
          self.comedi_subd, 
          self.comedi_ch, 
          c.COMEDI_OUTPUT)
    else:
      self.use_comedi = False

  async def get_inject_stream(self, name: str):
    queue = self.inject_analog_streams.get(name, None)
    if queue is None:
      queue = IterableQueue()
      self.stub.inject_analog(queue)
      await queue.put(thalamus_pb2.InjectAnalogRequest(node=name))
      self.inject_analog_streams[name] = queue
    return queue

  async def inject_analog(self, name: str, payload: thalamus_pb2.AnalogResponse):
    queue = await self.get_inject_stream(name)
    await queue.put(thalamus_pb2.InjectAnalogRequest(signal=payload))

  async def get_stim_stream(self, name: str):
    queue = self.stim_streams.get(name, None)
    if queue is None:
      queue = IterableQueue()
      self.stub.stim(queue)
      await queue.put(thalamus_pb2.StimRequest(node=thalamus_pb2.NodeSelector(name=name)))
      self.stim_streams[name] = queue
    return queue

  async def arm_stim(self, name: str, payload: thalamus_pb2.StimDeclaration):
    queue = await self.get_stim_stream(name)
    await queue.put(thalamus_pb2.StimRequest(inline_arm=payload))

  async def trigger_stim(self, name: str):
    queue = await self.get_stim_stream(name)
    await queue.put(thalamus_pb2.StimRequest(trigger=0))

  async def log(self, text: str):
    await self.log_queue.put(thalamus_pb2.Text(text=text,time=int(time.perf_counter()*1e9)))

  @property
  def behav_result(self) -> typing.Dict[str, typing.Any]:
    return self.trial_summary_data.behav_result

  @behav_result.setter
  def behav_result(self, value: typing.Dict[str, typing.Any]) -> None:
    self.trial_summary_data.behav_result = value

  def reset_trial_history(self) -> None:
    '''
    Resets the trial success/failure count
    '''
    self.trial_summary_data.trial_history.clear()
    self.config['status'] = ''

  def __on_queue_changed(self, _: ObservableCollection.Action,
                         _key: typing.Any, _value: typing.Any) -> None:
    """
    Updates the queue widget to reflect the state of the config's queue
    """
    i = 0
    for one in self.config['queue']:
      if 'tasks' in one:
        for two in one['tasks']:
          two['queue_index'] = i
          i += 1
      else:
        one['queue_index'] = i
        i += 1

  def do_yield(self) -> 'typing.Awaitable[None]':
    """
    Sleeps for one iteration of the event loop
    """
    return self.sleeper.do_yield()

  def sleep(self, duration: datetime.timedelta) -> 'typing.Awaitable[None]':
    """
    Sleeps until duration time has passed
    """
    return self.sleeper.sleep(duration)

  def until(self, condition: typing.Callable[[], bool]) -> 'typing.Awaitable[None]':
    """
    Sleeps until condition returns True
    """
    return self.sleeper.until(condition)

  def any(self, *args: 'typing.Awaitable[typing.Any]') -> 'typing.Awaitable[typing.Any]':
    """
    Sleeps until one of the futures finishes at which point the finished future is returned
    """
    return self.sleeper.any(*args)

  def subscribe(self, message_type: typing.Type[MESSAGE], topic: str,
                callback: typing.Callable[[MESSAGE], bool]) -> 'typing.Awaitable[MESSAGE]':
    '''
    Subscribe to a ROS topic
    '''
    return self.ros_manager.subscribe(message_type, topic, callback)

  def publish(self, message_type: typing.Type[MESSAGE], topic: str, message: MESSAGE) -> None:
    '''
    Publish a ROS message
    '''
    return self.ros_manager.publish(message_type, topic, message)

  def get_ros_parameter(self, key: str, parameter_type: typing.Type[PARAMETERTYPE]) -> PARAMETERTYPE:
    parameter = self.ros_manager.node.get_parameter(key)
    if parameter_type == str:
      return typing.cast(PARAMETERTYPE, parameter.get_parameter_value().string_value)
    if parameter_type == int:
      return typing.cast(PARAMETERTYPE, parameter.get_parameter_value().integer_value)
    if parameter_type == float:
      return typing.cast(PARAMETERTYPE, parameter.get_parameter_value().double_value)
    raise RuntimeError("Unsupported parameter type: {}".format(parameter_type))

  def get_color(self, key: str, default: typing.Optional[typing.List[int]] = None) -> QColor:
    """
    Reads a color from the current task_config.  The specified config value should be a list of numpers specifying RGB
    values.
    """
    rgb = self.task_config.get(key, default)
    self.trial_summary_data.used_values[key] = rgb
    red, green, blue = int(rgb[0]), int(rgb[1]), int(rgb[2])
    return QColor(red, green, blue)

  def get_value(self, key: str, default: typing.Any = None) -> typing.Union[int, float, bool]:
    """
    Reads a number from the current task_config.  The specified config value should be a number or an object specifying
    a max and min value.  In the latter case a uniform random number in the range [min, max] will be returned.
    """

    if default is None:
      default = {}
    value_config = self.task_config.get(key, default)

    if isinstance(value_config, (int, float, bool)):
      self.trial_summary_data.used_values[key] = value_config
      return value_config

    if ('min' in value_config or 'min' in default) and ('max' in value_config or 'max' in default):
      lower = value_config['min'] if 'min' in value_config else default['min']
      upper = value_config['max'] if 'max' in value_config else default['max']
      sampled_value = random.uniform(lower, upper)
      self.trial_summary_data.used_values[key] = sampled_value
      return sampled_value

    raise RuntimeError(f'Expected number or object with min and max fields, got {key}={value_config}')

  def get_target_value(self, itarg: int, key: str, default: typing.Any = None) -> typing.Union[int, float, bool, str]:
    """
    Reads or samples a number for a specific target in the task_config. The target identity is
    specified by itarg, the index to access the target from task_config['targets'][itarg]. The
    target config value should be a number or an object specifying a min and max value. In the latter
    case, a uniform random number in the range [min, max] will be returned.
    """
    if default is None:
      default = {}
    value_config = self.task_config['targets'][itarg].get(key, default)

    if isinstance(value_config, (int, float, bool, str)):
      self.trial_summary_data.used_values['targ' + str(itarg) + '_' + key] = value_config
      return value_config

    if ('min' in value_config or 'min' in default) and ('max' in value_config or 'max' in default):
      lower = value_config['min'] if 'min' in value_config else default['min']
      upper = value_config['max'] if 'max' in value_config else default['max']
      sampled_value = random.uniform(lower, upper)
      self.trial_summary_data.used_values['targ' + str(itarg) + '_' + key] = sampled_value
      return sampled_value

    raise RuntimeError(f'Expected number or object with min and max fields, got {key}={value_config}')

  def get_target_color(self, itarg: int, key: str,
                       default: typing.Optional[typing.List[int]] = None) -> QColor:
    """
    Reads a color from the current task_config for the target indexed by itarg.  The specified config value should be a
    list of numpers specifying RGB
    values.
    """
    rgb = self.task_config['targets'][itarg].get(key, default)
    self.trial_summary_data.used_values['targ' + str(itarg) + '_' + key] = rgb
    red, green, blue = int(rgb[0]), int(rgb[1]), int(rgb[2])
    return QColor(red, green, blue)

  def __sample_task(self, recurse: bool = False) -> typing.Any:
    """
    Attempts to sample a task from the first task or task_cluster in the queue.  If the queue is empty then a
    task_cluster is selected from task_clusters, weighted by the weight field, and added to the queue before sampling.
    """
    queue = self.config['queue']
    result = None
    LOGGER.info('GETTING TASK FROM QUEUE')
    while queue and result is None:
      child = queue[0]
      if child['type'] == 'task_cluster':
        result = sample_from_cluster(child)
      elif child['type'] == 'task':
        result = sample_from_task(child)

      if not result:
        del queue[0]

    if not result:
      LOGGER.info('GETTING TASK CLUSTER TO ADD TO QUEUE')
      clusters = self.config['task_clusters']
      weights = [compute_cluster_weight(cluster) for cluster in clusters]
      LOGGER.info('%s %s', [t['name'] for t in clusters], weights)
      if sum(weights) > .5:
        selected = random.choices(clusters, weights=weights, k=1)[0]
        LOGGER.info('GOT %s', selected["name"])
        #LOGGER.info(json.dumps(selected.unwrap(), indent=2))
        selected_copy = selected.copy()
        for task in selected_copy.get('tasks', []):
          task['task_cluster_name'] = selected_copy['name']
        queue.append(selected_copy)
        result = self.__sample_task(True)

    if not recurse:
      LOGGER.info('QUEUE STATUS')
      for cluster in queue:
        if cluster['type'] == 'task_cluster':
          LOGGER.info('  CLUSTER=%s', cluster["name"])
          for task in cluster['tasks']:
            LOGGER.info('    TASK=%s, GOAL=%s', task["name"], task["goal"])
        else:
          LOGGER.info('  TASK=%s, GOAL=%s', cluster["name"], cluster["goal"])
    return result

  async def __execute_remote_task(self, task_config: ObservableCollection) -> TaskResult:
    '''
    Publish task config to ROS2 for a remote task executor to handle.
    '''
    LOGGER.info('waiting for executor')
    await self.servicer.wait_for_executor()
    config = task_controller_pb2.TaskConfig(body=json.dumps(task_config.unwrap()))
    await self.servicer.send_config(config)

    LOGGER.info('waiting on task')

    try:
      grpc_result = await self.servicer.get_result()
      result = TaskResult(grpc_result.success)
    except ExecutorLostError:
      LOGGER.error('Task executor disconnected')
      result = TaskResult(False)

    return result

  def __update_status(self, success: bool) -> None:
    assert self.task_config is not None, 'self.task_config is None'

    if 'task_cluster_name' in self.task_config:
      cluster_name = self.task_config.get('task_cluster_name', None)
    else:
      LOGGER.error('Trying to update status but tasks is missing cluster name')
      return

    task_name = cluster_name + '/' + self.task_config['name']
    self.trial_summary_data.trial_history[task_name]['success' if success else 'failure'] += 1
    items = sorted(self.trial_summary_data.trial_history.items())
    status = ' '.join(f'{b[0]} = {b[1]["success"]}/{b[1]["failure"]}' for b in items)
    self.config['status'] = status

  def get_canvas_info(self) -> None:
    """
    Returns a dictionary of fields related to the gaze/touch transforms and the canvas they are mapped to.
    This is used as part of the trial summary, which is later used in data analysis to reconstruct
    the gaze and touch positions.
    """
    if self.widget and self.widget.input_config:
      gaze_tforms= []
      for tr in self.widget.input_config.gaze_transforms:
        gaze_tforms.append([tr.m11(), tr.m12(), tr.m13(), tr.m21(), tr.m22(), tr.m23(), tr.m31(), tr.m32(), tr.m33()])

      tr = self.widget.input_config.touch_calibration.touch_transform
      touch_tform = [tr.m11(), tr.m12(), tr.m13(), tr.m21(), tr.m22(), tr.m23(), tr.m31(), tr.m32(), tr.m33()]

      transforms = {}
      transforms['gaze_tforms'] = gaze_tforms
      transforms['touch_tform'] = touch_tform
      transforms['dpi'] = self.config.get('dpi', None) or self.widget.logicalDpiX()      
      transforms['frame_width'] = self.widget.frameGeometry().width()
      transforms['frame_height'] = self.widget.frameGeometry().height()
      
      local_to_global_translation = self.widget.mapFromGlobal(QPoint(0, 0))
      transforms['local_to_global_translation_x'] = local_to_global_translation.x()
      transforms['local_to_global_translation_y'] = local_to_global_translation.y()

      return transforms
    else:
      return {}

  def pulse_digital_channel(self):
    if self.use_comedi:
      c.comedi_dio_write(self.it, self.comedi_subd, self.comedi_ch, 1)
      #await asyncio.sleep(0.001)
      time.sleep(0.001)
      c.comedi_dio_write(self.it, self.comedi_subd, self.comedi_ch, 0)
    
  async def run(self) -> None:
    """
    Implements the TaskContext loop.  Will repeatedly select and run a task until stop is called.
    """
    self.running, self.sleeper.running = True, True
    sleeper_task = create_task_with_exc_handling(self.sleeper.run())
    while True:
      self.task_config = self.__sample_task()
      if self.task_config is None:
        LOGGER.info('NO TASK')
        try:
          await self.sleep(datetime.timedelta(seconds=1))
          continue
        except asyncio.CancelledError:
          break
      LOGGER.info('RUNNING TASK %s', self.task_config["name"])
      was_cancelled = False

      self.trial_summary_data.behav_result.clear()
      self.trial_summary_data.used_values.clear()

      if self.widget:
        task = self.task_descriptions_map[self.task_config['task_type']]
        try:
          LOGGER.info('TRIAL START')
          await self.log('TRIAL START ' + self.task_config["type"] + ' ' + self.task_config["name"])
          result = await task.run(self)
          await self.log('TRIAL FINISHED ' + self.task_config["type"] + ' ' + self.task_config["name"])
          self.__update_status(result.success)
          LOGGER.info('TRIAL FINISH')
        except asyncio.CancelledError:
          LOGGER.info('CANCELLED')
          was_cancelled = True
        self.widget.renderer = lambda w: None
        self.widget.touch_listener = lambda e: None
        self.widget.gaze_listener = lambda e: None
        self.widget.key_release_handler = lambda e: None
        if self.config.get('eye_scaling', {}).get('Auto Clear', False):
          self.widget.clear_accumulation()
        self.widget.update()
      else:
        try:
          result = await self.__execute_remote_task(self.task_config)
        except asyncio.CancelledError:
          LOGGER.info('CANCELLED')
          was_cancelled = True
      
      if not was_cancelled and result.success:
        self.task_config['goal'] -= 1
        current_index = self.config['reward_schedule']['index']
        modulus = min(len(s) for s in self.config['reward_schedule']['schedules'])
        self.config['reward_schedule']['index'] = (current_index + 1) % modulus
      if not self.sleeper.running:
        break

      if not was_cancelled:
        trial_summ = {
          'used_values': self.trial_summary_data.used_values,
          'task_config': {**self.task_config.unwrap(), **self.get_canvas_info()},
          'task_result': result._asdict()
        }

        if self.trial_summary_data.behav_result:
          trial_summ['behav_result'] = self.trial_summary_data.behav_result
        await self.log(json.dumps(trial_summ))
    
    for future in self.sleeper.cancelled_futures:
      future.exception()
    self.sleeper.running = False
    await sleeper_task
    
    self.running = False
    #asyncio.get_event_loop().wake()
    LOGGER.info('WAKE')

  def start(self) -> None:
    """
    Start the task loop
    """
    if self.running:
      return

    self.task = create_task_with_exc_handling(self.run())

  def cancel(self) -> None:
    """
    Aborts the current task
    """
    self.sleeper.cancel()

  def stop(self) -> 'asyncio.tasks.Task[typing.Any]':
    """
    Schedules the TaskContext event loop to stop and waits
    """
    if not self.running:
      return asyncio.get_event_loop().create_task(asyncio.sleep(0))

    if self.servicer is not None:
      self.servicer.stop()
    self.sleeper.stop()
    return self.task

  def get_reward(self, channel: int) -> float:
    """
    Returns the current reward value for a given channel
    """
    current_index = self.config['reward_schedule']['index']
    reward = float(self.config['reward_schedule']['schedules'][int(channel)][current_index])
    self.trial_summary_data.used_values['reward'] = reward
    return max(reward, 0.0)

  def process(self) -> None:
    '''
    Progress task execution
    '''
    if not self.sleeper.run_future.done():
      self.sleeper.run_future.set_result(None)

  def get_channel(self, name: str):
    if name in self.channels:
      return self.channels[name]
    
    channel = grpc.aio.insecure_channel(name)
    self.channels[name] = channel
    return channel

  def thalamus_stub(self) -> thalamus_pb2_grpc.ThalamusStub:
    return self.stub

  async def cleanup(self):
    for channel in self.channels.values():
      await channel.close()
    self.channels = {}

