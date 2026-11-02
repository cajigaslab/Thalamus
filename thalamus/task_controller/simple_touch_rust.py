"""
Delegate task: simple_touch rendered by the low-latency Rust executor.

Same pattern as joystick_intro_rust.py (see its docstring and
docs/rust_bci_patch.md): a THIN delegate running in TaskContext.run's
`if self.widget:` branch, so Python still emits TRIAL START/FINISHED and
trial_summ, while the long-lived Rust executor renders the subject display,
logs the BehavState markers and delivers the reward.

What differs from the joystick delegate:

  * All random draws happen HERE, through the same context.get_value /
    get_target_value / get_target_color calls in the same order as
    simple_touch_task.run. Those getters record into trial_summ's used_values,
    so sampling in Python keeps used_values identical to the Python task. The
    realized values travel to Rust as TrialConfig.resolved_json.
  * Touch is NOT forwarded from the canvas: the executor subscribes to the
    touch-screen node itself (lower latency, no 16 ms Qt tick).
  * Intentional behavior change vs simple_touch_task.py: on a fail the target
    is hidden and the photodiode square goes dark (Python's fail_trial misses
    `nonlocal`, leaving both unchanged). See rust/joystick_task/src/touch.rs.
"""

import os
import json
import time
import asyncio
import logging

import numpy as np

from ..qt import *
from ..util import IterableQueue
from .util import TaskContextProtocol, TaskResult, get_sound
from . import simple_touch_layout
from .simple_touch_task import RANDOM_DEFAULT, COLOR_DEFAULT

LOGGER = logging.getLogger(__name__)

# 127.0.0.1, NOT localhost: gRPC tries ::1 first and eats a 1 s fallback.
RUST_TASK_ENDPOINT = '127.0.0.1:50060'

# Target table + visual layout editor (simple_touch_layout.py).
create_widget = simple_touch_layout.create_widget


def _resolve(context: TaskContextProtocol) -> dict:
  """Sample this trial's values exactly as simple_touch_task.run does.

  The call order below mirrors simple_touch_task.py:202-245 and 265-266 so the
  random draws and the used_values insertion order are the same.
  """
  timeouts = [context.get_value(key, RANDOM_DEFAULT) for key in (
    'intertrial_timeout', 'start_timeout', 'hold_timeout',
    'blink_timeout', 'fail_timeout', 'success_timeout')]
  is_choice = context.get_value('is_choice')
  no_start_fail = context.get_value('no_start_fail')

  ntargets = len(context.task_config['targets'])
  dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()

  # get_target_rectangles (@154-180)
  geometry = []
  for itarg in range(ntargets):
    radius = context.get_target_value(itarg, 'radius')
    angle = context.get_target_value(itarg, 'angle')
    width = context.get_target_value(itarg, 'width')
    height = context.get_target_value(itarg, 'height')
    geometry.append((radius, angle, width, height))
  windows = [context.get_target_value(i, 'window_size') for i in range(ntargets)]
  colors = [context.get_target_color(i, 'color', COLOR_DEFAULT) for i in range(ntargets)]
  for i in range(ntargets):
    context.get_target_value(i, 'on_luminance', COLOR_DEFAULT)
  for i in range(ntargets):
    context.get_target_value(i, 'off_luminance', COLOR_DEFAULT)
  for i in range(ntargets):
    context.get_target_value(i, 'name', None)
  reward_channels = [context.get_target_value(i, 'reward_channel', None) for i in range(ntargets)]
  stl_files = [context.get_target_value(i, 'stl_file') for i in range(ntargets)]

  # Disabled targets (layout editor) are never presented. Read the flag
  # straight from the config: the Python task has no such key, so it must not
  # show up in used_values. With every target enabled this is the same draw as
  # simple_touch_task's np.random.randint(ntargets).
  enabled_ids = [i for i in range(ntargets)
                 if bool(context.task_config['targets'][i].get('enabled', True))]
  presented_index = enabled_ids[int(np.random.randint(len(enabled_ids)))] if enabled_ids else -1

  targets = []
  for i, (radius, angle, width, height) in enumerate(geometry):
    targets.append({
      'width': float(width),
      'height': float(height),
      'radius': float(radius),
      'angle': float(angle),
      'window_size': float(windows[i]),
      'color': [colors[i].red(), colors[i].green(), colors[i].blue()],
      'reward_channel': float(reward_channels[i]),
      'enabled': i in enabled_ids,
    })

  return {
    'intertrial_timeout': float(timeouts[0]),
    'start_timeout': float(timeouts[1]),
    'hold_timeout': float(timeouts[2]),
    'blink_timeout': float(timeouts[3]),
    'fail_timeout': float(timeouts[4]),
    'success_timeout': float(timeouts[5]),
    'is_choice': bool(is_choice),
    'no_start_fail': bool(no_start_fail),
    'state_indicator_x': int(context.task_config['state_indicator_x']),
    'state_indicator_y': int(context.task_config['state_indicator_y']),
    # Task center as screen fractions (layout editor). Read from the config
    # directly: the Python task has no such keys, so they stay out of
    # used_values.
    'center_x': min(1.0, max(0.0, float(context.task_config.get('center_x', 0.5)))),
    'center_y': min(1.0, max(0.0, float(context.task_config.get('center_y', 0.5)))),
    'dpi': float(dpi),
    'presented_index': presented_index,
    'targets': targets,
    # Not sent to Rust's schema; checked below.
    '_stl_files': stl_files,
  }


def _reward_ms(context: TaskContextProtocol) -> list:
  """Per-channel pulse durations at the current schedule index.

  Read directly rather than via context.get_reward(), which would record
  used_values['reward'] for every channel (and on fails). Python truncates
  with int(get_reward(ch)) (@383), so do the same here.
  """
  schedule = context.config['reward_schedule']
  index = schedule['index']
  result = []
  for values in schedule['schedules']:
    try:
      result.append(float(int(max(float(values[index]), 0.0))))
    except (IndexError, TypeError, ValueError):
      result.append(0.0)
  return result


async def run(context: TaskContextProtocol) -> TaskResult:
  """Delegate one simple_touch trial to the Rust executor."""
  task_config = context.task_config
  resolved = _resolve(context)
  if any(resolved.pop('_stl_files')):
    LOGGER.error('simple_touch_rust does not render STL targets; clear the targets\' '
                 'Stl File or use the Python Simple Touch task. Failing trial.')
    return TaskResult(success=False)
  if resolved['presented_index'] < 0:
    LOGGER.error('simple_touch_rust: every target is disabled. Enable at least one '
                 'target in the layout editor. Failing trial.')
    return TaskResult(success=False)

  success_sound = get_sound(os.path.join(os.path.dirname(__file__), 'success_clip.wav'))
  fail_sound = get_sound(os.path.join(os.path.dirname(__file__), 'failure_clip.wav'))

  # Lazy imports so this module is import-safe before codegen exists.
  try:
    import grpc
    from .. import rust_task_pb2, rust_task_pb2_grpc
  except Exception as exc:  # ImportError until the stubs are generated
    LOGGER.error('Rust task stubs unavailable (%s). Generate rust_task_pb2 and start the '
                 'executor before selecting the Rust task. Failing trial.', exc)
    return TaskResult(success=False)

  # IterableQueue, NOT an async generator (see joystick_intro_rust.py: closing a
  # suspended async generator from grpc.aio's teardown kills task_controller).
  request_queue = IterableQueue()

  # --- operator live mirror + window overlay ---

  mirror_state = {'image': None, 'trial_info': None}

  async def mirror_consumer(stub) -> None:
    try:
      async for frame in stub.frames(rust_task_pb2.FrameRequest(max_hz=30, max_width=640)):
        image = QImage.fromData(bytes(frame.jpeg), 'JPEG')
        if not image.isNull():
          mirror_state['image'] = image
          context.widget.update()
    except asyncio.CancelledError:
      raise
    except grpc.aio.AioRpcError as exc:
      LOGGER.warning('operator mirror stream ended: %s %s', exc.code(), exc.details())

  def renderer(painter) -> None:
    image = mirror_state['image']
    w = context.widget.width()
    h = context.widget.height()
    if image is None or w <= 0 or h <= 0:
      painter.setPen(QPen(QColor(255, 255, 255), 1))
      painter.drawText(10, 20, 'Simple Touch (Rust executor)')
      return
    scale = min(w / image.width(), h / image.height())
    dw = int(image.width() * scale)
    dh = int(image.height() * scale)
    ox, oy = (w - dw) // 2, (h - dh) // 2
    painter.drawImage(QRect(ox, oy, dw, dh), image)
    painter.setPen(QPen(QColor(255, 255, 255), 1))
    painter.drawText(10, 20, 'Simple Touch (Rust executor)')

    # Acceptance windows, operator-only (simple_touch_task.py:311-317). The
    # mirror is downscaled from the subject display, whose coordinates the
    # executor reports in subject pixels; image.width() is that width scaled by
    # mirror_scale.
    info = mirror_state['trial_info']
    if info and info.get('targets'):
      subject_w = info.get('subject_width') or image.width()
      to_widget = scale * image.width() / subject_w
      path = QPainterPath()
      for target in info['targets']:
        cx, cy = target['center']
        window = float(target['window']) * to_widget
        path.addEllipse(QPointF(ox + cx * to_widget, oy + cy * to_widget), window, window)
      painter.fillPath(path, QColor(255, 255, 255, 128))

  context.widget.renderer = renderer
  context.widget.touch_listener = lambda cursor: None
  context.widget.key_press_handler = lambda event: None
  context.widget.key_release_handler = lambda event: None
  context.widget.update()

  request_queue.queue.put_nowait(rust_task_pb2.TrialRequest(config=rust_task_pb2.TrialConfig(
    config_json=json.dumps(task_config.unwrap()),
    reward_ms=_reward_ms(context),
    reward_scale=1.0,
    # Clock-sync seed: same clock Python logs with (task_context.py:409).
    python_perf_ns=int(time.perf_counter() * 1e9),
    resolved_json=json.dumps(resolved),
  )))

  success = False
  async with grpc.aio.insecure_channel(RUST_TASK_ENDPOINT) as channel:
    stub = rust_task_pb2_grpc.RustTaskStub(channel)
    mirror_task = asyncio.get_event_loop().create_task(mirror_consumer(stub))
    try:
      async for event in stub.run_trial(request_queue):
        kind = event.WhichOneof('body')
        if kind == 'marker':
          text = event.marker.text
          if text.startswith('TrialInfo='):
            try:
              mirror_state['trial_info'] = json.loads(text[len('TrialInfo='):])
            except json.JSONDecodeError:
              LOGGER.exception('Malformed TrialInfo marker from Rust executor')
            context.widget.update()
          else:
            # Rust already logged this to Thalamus.log. Do NOT context.log() it
            # (would duplicate the record in the capture file).
            LOGGER.debug('BehavState marker: %s @ %d', text, event.marker.time_ns)
            if text == 'BehavState=success':
              success_sound.play()
            elif text == 'BehavState=fail':
              fail_sound.play()
        elif kind == 'behav_result_json':
          try:
            context.behav_result = json.loads(event.behav_result_json)
          except json.JSONDecodeError:
            LOGGER.exception('Malformed behav_result_json from Rust executor')
        elif kind == 'success':
          success = event.success
    except grpc.aio.AioRpcError as exc:
      # A dead/rejecting executor must FAIL THE TRIAL, not kill task_controller.
      LOGGER.error(
        'Rust executor RPC failed (%s: %s). Is joystick_intro_executor '
        'running on %s? Its terminal has the specific error.',
        exc.code(), exc.details(), RUST_TASK_ENDPOINT,
      )
      return TaskResult(success=False)
    finally:
      mirror_task.cancel()
      try:
        await mirror_task
      except (asyncio.CancelledError, Exception):  # noqa: BLE001 - never mask the trial result
        pass
      await request_queue.close()

  if success:
    # Python's success branch calls get_reward on the selected target's
    # channel (@383), recording used_values['reward']; do the same so
    # trial_summ matches. The schedule index only advances after the trial
    # returns, so this is the value Rust delivered.
    selected = context.behav_result.get('selected_target_id')
    if selected is not None:
      context.get_reward(resolved['targets'][selected]['reward_channel'])

  return TaskResult(success=success)
