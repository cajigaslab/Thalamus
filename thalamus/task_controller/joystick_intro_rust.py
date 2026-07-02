"""
Delegate task: joystick_intro rendered by the low-latency Rust executor.

This is the Python side of the real-time BCI patch. It is a THIN delegate:

  * create_widget  -> reuses joystick_intro.create_widget verbatim, so the
                      operator edits the exact same config (targets, influence
                      sliders, free-play rewards, layout editor). No new UI.
  * run            -> instead of rendering in Qt, forwards the resolved config to
                      the long-lived Rust executor over the RustTask gRPC service
                      (default localhost:50060) as a BIDIRECTIONAL stream:
                      the first request carries TrialConfig; subsequent requests
                      forward operator inputs the Rust process cannot observe
                      (arrow-key override, the free-play end key, touch events).
                      Behavioral events stream back; run returns TaskResult.

Why a delegate (not the built-in remote-executor seam): the TaskController
`execution` stream is only consumed when task_controller has no local widget
(task_context.py:696 vs :717), which never happens on the operator rig, and its
single executor slot would hijack every task type. Running as a normal task in
the `if self.widget:` branch keeps Python emitting TRIAL START/FINISHED and the
trial_summ exactly as today; Rust emits the BehavState markers to its own log
stream and returns behav_result for Python to serialize.

See docs/rust_bci_patch.md for the full design.
"""

import os
import json
import time
import asyncio
import logging

from ..qt import *
from ..util import IterableQueue
from .util import TaskContextProtocol, TaskResult, get_sound
from . import joystick_intro

LOGGER = logging.getLogger(__name__)

# The Rust executor hosts the RustTask service here (see rust/README.md).
# 127.0.0.1, NOT localhost: gRPC tries ::1 first and eats a 1 s fallback.
RUST_TASK_ENDPOINT = '127.0.0.1:50060'

# Reuse the existing operator config panel unchanged.
create_widget = joystick_intro.create_widget


def _resolve_reward_ms(context: TaskContextProtocol) -> list:
    """Resolve per-channel reward pulse durations (ms) via the shared schedule.

    Python owns the reward schedule; Rust only shapes the pulse. We resolve every
    channel the task might use so the executor can index by reward_channel.
    """
    try:
        schedules = context.config['reward_schedule']['schedules']
        return [float(context.get_reward(ch)) for ch in range(len(schedules))]
    except Exception:  # pragma: no cover - defensive; empty means "no reward resolved"
        LOGGER.exception('Could not resolve reward schedule; sending empty reward_ms')
        return []


async def run(context: TaskContextProtocol) -> TaskResult:
    """Delegate one trial to the Rust executor.

    Runs inside TaskContext.run's `if self.widget:` branch, so TRIAL START/
    FINISHED bracketing and trial_summ (task_context.py:701-741) still happen in
    Python. We only replace rendering + the behavioral event source.
    """
    task_config = getattr(context, 'task_config', context.config['queue'][0])
    reward_scale = float(task_config.get('reward_scale', 1.0))
    reward_ms = _resolve_reward_ms(context)
    free_play_end_key = str(task_config.get('free_play_end_key', 'space'))

    success_sound = get_sound(os.path.join(os.path.dirname(__file__), 'success_clip.wav'))
    fail_sound = get_sound(os.path.join(os.path.dirname(__file__), 'failure_clip.wav'))

    # Lazy imports so this module is import-safe before codegen exists.
    try:
        import grpc
        from .. import rust_task_pb2, rust_task_pb2_grpc  # generated from proto/rust_task.proto
    except Exception as exc:  # ImportError until the stubs are generated
        LOGGER.error(
            'Rust task stubs unavailable (%s). Generate rust_task_pb2 and start the '
            'joystick_intro_executor before selecting the Rust task. Failing trial.',
            exc,
        )
        return TaskResult(success=False)

    # --- operator input forwarding (mirrors joystick_intro.py:2227-2277) ---

    # IterableQueue, NOT an async generator: grpc.aio tears the request
    # iterator down from another task at RPC completion, and closing a
    # suspended async GENERATOR raises "aclose(): asynchronous generator is
    # already running", killing the whole task_controller (live-session crash
    # 2026-07-02). task_context.py streams through IterableQueue for the same
    # reason.
    request_queue = IterableQueue()

    def send_operator(event: 'rust_task_pb2.OperatorEvent') -> None:
        request_queue.queue.put_nowait(rust_task_pb2.TrialRequest(operator=event))

    arrow_names = {
        Qt.Key.Key_Left: 'left',
        Qt.Key.Key_Right: 'right',
        Qt.Key.Key_Up: 'up',
        Qt.Key.Key_Down: 'down',
    }

    def on_key_press(event: QKeyEvent) -> None:
        if event.isAutoRepeat():
            return
        name = arrow_names.get(event.key())
        if name:
            send_operator(rust_task_pb2.OperatorEvent(
                arrow=rust_task_pb2.ArrowKey(key=name, pressed=True)))

    def on_key_release(event) -> None:
        key = event.key()
        name = arrow_names.get(key)
        if name:
            send_operator(rust_task_pb2.OperatorEvent(
                arrow=rust_task_pb2.ArrowKey(key=name, pressed=False)))
        # Same evaluation as joystick_intro.on_key_release: every release SETS
        # the end-requested flag (true only when it matches the end key).
        if free_play_end_key == 'q':
            end = key == Qt.Key.Key_Q
        elif free_play_end_key == 'enter':
            end = key in (Qt.Key.Key_Return, Qt.Key.Key_Enter)
        else:
            end = key == Qt.Key.Key_Space
        send_operator(rust_task_pb2.OperatorEvent(end_requested=end))

    def on_touch(cursor: QPoint) -> None:
        # The touch node streams (-1, -1) "no touch" updates continuously;
        # Python's touch_handler ignores x<0 and so does the Rust port — skip
        # forwarding the spam.
        if cursor.x() < 0:
            return
        send_operator(rust_task_pb2.OperatorEvent(
            touch=rust_task_pb2.Touch(x=cursor.x(), y=cursor.y())))

    # --- operator live mirror (M4): paint executor-streamed JPEG frames ---

    mirror_state = {'image': None}

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
        if image is not None and w > 0 and h > 0:
            # Scale to fit, preserving the subject display's aspect ratio.
            scale = min(w / image.width(), h / image.height())
            dw = int(image.width() * scale)
            dh = int(image.height() * scale)
            painter.drawImage(QRect((w - dw) // 2, (h - dh) // 2, dw, dh), image)
        painter.setPen(QPen(QColor(255, 255, 255), 1))
        painter.drawText(10, 20, 'Joystick Intro (Rust executor)')

    context.widget.renderer = renderer
    context.widget.key_press_handler = on_key_press
    context.widget.key_release_handler = on_key_release
    context.widget.touch_listener = on_touch
    context.widget.setFocus()
    context.widget.update()

    config_message = rust_task_pb2.TrialRequest(config=rust_task_pb2.TrialConfig(
        config_json=json.dumps(task_config.unwrap()),
        reward_ms=reward_ms,
        reward_scale=reward_scale,
        # Clock-sync seed: same clock Python logs with (task_context.py:409).
        python_perf_ns=int(time.perf_counter() * 1e9),
    ))
    request_queue.queue.put_nowait(config_message)

    success = False
    async with grpc.aio.insecure_channel(RUST_TASK_ENDPOINT) as channel:
      stub = rust_task_pb2_grpc.RustTaskStub(channel)
      mirror_task = asyncio.get_event_loop().create_task(mirror_consumer(stub))
      try:
        async for event in stub.run_trial(request_queue):
            kind = event.WhichOneof('body')
            if kind == 'marker':
                # Rust already logged this to Thalamus.log; here it drives the
                # operator-side sounds only. Do NOT context.log() it (would
                # duplicate the record in the capture file).
                LOGGER.debug('BehavState marker: %s @ %d', event.marker.text, event.marker.time_ns)
                if event.marker.text == 'BehavState=success':
                    success_sound.play()
                elif event.marker.text == 'BehavState=fail':
                    fail_sound.play()
            elif kind == 'behav_result_json':
                try:
                    context.behav_result = json.loads(event.behav_result_json)
                except json.JSONDecodeError:
                    LOGGER.exception('Malformed behav_result_json from Rust executor')
            elif kind == 'config_updates_json':
                # Cross-trial bookkeeping Python normally persists by mutating
                # task_config (_streak_count, _last_cursor_x/y, ...).
                try:
                    for key, value in json.loads(event.config_updates_json).items():
                        task_config[key] = value
                except json.JSONDecodeError:
                    LOGGER.exception('Malformed config_updates_json from Rust executor')
            elif kind == 'success':
                success = event.success
      except grpc.aio.AioRpcError as exc:
        # A dead/rejecting executor must FAIL THE TRIAL, not kill the whole
        # task_controller. Note: if the executor rejects the config, the
        # client-side request-sender race can report INTERNAL here instead of
        # the real status — check the executor's terminal for the actual
        # rejection reason.
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

    return TaskResult(success=success)
