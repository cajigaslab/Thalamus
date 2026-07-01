"""
Delegate task: joystick_intro rendered by the low-latency Rust executor.

This is the Python side of the real-time BCI patch. It is a THIN delegate:

  * create_widget  -> reuses joystick_intro.create_widget verbatim, so the
                      operator edits the exact same config (targets, influence
                      sliders, free-play rewards, layout editor). No new UI.
  * run            -> instead of rendering in Qt, forwards the resolved config to
                      the long-lived Rust executor over the RustTask gRPC service
                      (default localhost:50060), streams behavioral events back,
                      and returns TaskResult.

Why a delegate (not the built-in remote-executor seam): the TaskController
`execution` stream is only consumed when task_controller has no local widget
(task_context.py:696 vs :717), which never happens on the operator rig, and its
single executor slot would hijack every task type. Running as a normal task in
the `if self.widget:` branch keeps Python emitting TRIAL START/FINISHED and the
trial_summ exactly as today; Rust emits the BehavState markers to its own log
stream and returns behav_result for Python to serialize.

See docs/rust_bci_patch.md for the full design.

STATUS: scaffold. The wire call is guarded so this module imports safely even
before the Rust `rust_task` Python stubs are generated. When the executor and
stubs are ready, remove the guard and register this in tasks.py:

    from . import joystick_intro_rust
    TaskDescription('joystick_intro_rust', 'Joystick Intro (Rust)',
      joystick_intro_rust.create_widget, joystick_intro_rust.run),
"""

import json
import time
import logging

from .util import TaskContextProtocol, TaskResult
from . import joystick_intro

LOGGER = logging.getLogger(__name__)

# The Rust executor hosts the RustTask service here (see rust/README.md).
RUST_TASK_ENDPOINT = 'localhost:50060'

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

    # Lazy imports so this module is import-safe before codegen exists.
    try:
        import grpc
        from .. import rust_task_pb2, rust_task_pb2_grpc  # generated from rust/.../proto/rust_task.proto
    except Exception as exc:  # ImportError until the Rust stubs are generated
        LOGGER.error(
            'Rust task stubs unavailable (%s). Generate rust_task_pb2 and start the '
            'joystick_intro_executor before selecting the Rust task. Failing trial.',
            exc,
        )
        return TaskResult(success=False)

    request = rust_task_pb2.TrialConfig(
        config_json=json.dumps(task_config.unwrap()),
        reward_ms=reward_ms,
        reward_scale=reward_scale,
        # Clock-sync seed: same clock Python logs with (task_context.py:409).
        python_perf_ns=int(time.perf_counter() * 1e9),
    )

    async with grpc.aio.insecure_channel(RUST_TASK_ENDPOINT) as channel:
        stub = rust_task_pb2_grpc.RustTaskStub(channel)
        success = False
        async for event in stub.run_trial(request):
            kind = event.WhichOneof('body')
            if kind == 'marker':
                # Rust already logged this to Thalamus.log; here it is only for the
                # operator display / diagnostics. Do NOT context.log() it (would
                # duplicate the record in the capture file).
                LOGGER.debug('BehavState marker: %s @ %d', event.marker.text, event.marker.time_ns)
            elif kind == 'behav_result_json':
                try:
                    context.behav_result = json.loads(event.behav_result_json)
                except json.JSONDecodeError:
                    LOGGER.exception('Malformed behav_result_json from Rust executor')
            elif kind == 'success':
                success = event.success

        return TaskResult(success=success)
