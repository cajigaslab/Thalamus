# Real-Time BCI Patch: Low-Latency Rust `joystick_intro`

Authoritative design doc for the Rust executor that renders the `joystick_intro`
behavioral task with near-single-frame latency. Companion to `rust/README.md`
(build/status) and the code under `rust/joystick_task/`.

## Context / problem

The `joystick_intro` task runs inside the Python/PyQt6 `task_controller`.
Photodiode measurements show ~50 ms command-to-photon latency on a 240 Hz
(4.2 ms/frame) Linux subject display. Goal: drive command-to-photon toward one
display frame while preserving exactly how trials are configured, how behavior is
logged, and how downstream analysis reads the data.

The 50 ms is **architectural, not "Python is slow"**, and stacks from unsynchronized
stages:

- Task loop sleeps 10 ms/tick: `joystick_intro.py:3235` (`await context.sleep(0.01)`)
  after requesting a repaint at `:3234` (`context.widget.update()`).
- A hand-rolled Qt/asyncio super-loop pumps paint events only every ~16 ms and is
  **not** phase-locked to the task loop: `main_impl.py:236-239`.
- `Canvas(QOpenGLWidget)` renders into an offscreen FBO Qt then composites (+~1
  frame): `canvas.py:448`, `paintGL` at `:627` calling the task renderer at `:643`.
- vsync/buffer swap adds up to another frame.
- Input side: the joystick analog gRPC stream is only serviced when the asyncio
  loop runs, gated by the same 16 ms tick (`analog_processor` at
  `joystick_intro.py:2299`, stream opened at `:2848-2855`).

A Rust process that owns a tight vsync-paced loop (sample input -> step state ->
render -> present) on an unredirected Linux fullscreen surface collapses these into
~1-1.5 frames.

## Approach

**Standalone Rust executor process**, connected to the existing infrastructure
entirely over gRPC — no changes to the C++ core (`src/`) and no changes to Python
packaging. Same integration pattern as the existing .NET (`dotnet/dotnet/NodeGraph.cs`)
and Java (`java/app/.../App.java:29`) clients: open an **insecure** channel to
`localhost:50050`, instantiate the `Thalamus` stub, stream.

### Control handoff — thin Python delegate (NOT the remote-executor seam)

The built-in remote-executor seam (`TaskControllerServicer.execution` at
`servicer.py:77`, `TaskContext.__execute_remote_task` at `task_context.py:601`) is
**not usable here**. Verified in `task_context.py:696-722`: the `execution` stream is
only consumed in the `else` branch, entered **only when `self.widget is None`**. On
the operator rig the Canvas widget always exists, so that branch never runs; and
there is a single executor slot (`out_queue`/`in_queue`), so routing through it would
hijack *every* task type.

Instead: a **thin Python delegate task** `thalamus/task_controller/joystick_intro_rust.py`,
registered in `tasks.py` as its own `TaskDescription` (`task_type ==
"joystick_intro_rust"`). Its `run(context)` runs inside the normal `if self.widget:`
branch (`task_context.py:696`), so Python still emits `TRIAL START`/`TRIAL FINISHED`
(`:701-703`) and `trial_summ` (`:732-741`) exactly as today. Instead of rendering
locally, `run` forwards the resolved config to the long-lived Rust process and
streams behavioral events back. Only this one task type delegates; all existing
Python-rendered tasks are untouched. It reuses `joystick_intro.create_widget`
verbatim, so the operator config workflow is identical.

### Log-ownership split (byte-compatibility)

| Record | Emitter | Stream |
|---|---|---|
| `TRIAL START` / `TRIAL FINISHED` | Python (unchanged, `task_context.py:701-703`) | Python `log_queue` |
| `BehavState=…` markers | **Rust**, at the transition instant | Rust's own `Thalamus.log` stream |
| `trial_summ` incl. `behav_result` | Python (unchanged, `task_context.py:733-741`) | Python `log_queue` |

Rust builds `behav_result` (replicating `append_event`/`finalize_attempt`,
`joystick_intro.py:2409-2437`) and streams it back to the delegate, which sets
`context.behav_result` (`task_context.py:415`) so Python's existing `trial_summ`
serialization emits it. Because the storage node writes Text records with an
**empty `node` field regardless of sender** (`src/storage2_node.cpp:146-163`,
`on_log` never calls `set_node`), Rust- and Python-emitted Text records merge
transparently in the capture; `record_reader` is time-ordered and content-based, so
per-record content + correct `Text.time` is what matters. **Do not double-log**: the
delegate must NOT `context.log()` the markers Rust already sent.

### Rust-hosted control channel (`RustTask`, `proto/rust_task.proto`)

Because `TaskController.execution` is unavailable, the delegate <-> Rust link is a
new small gRPC service **hosted by the Rust process** (`RustTask` on
`localhost:50060`), delegate as client:

- `run_trial(TrialConfig) -> stream TrialEvent`: `TrialConfig` carries `config_json`
  (`json.dumps(task_config.unwrap())`), resolved per-channel `reward_ms`,
  `reward_scale`, and a `python_perf_ns` clock-sync seed. `TrialEvent` streams back
  `BehavMarker`s (operator display), the final `behav_result_json`, and terminal
  `success`.
- `frames(FrameRequest) -> stream MirrorFrame`: operator live-mirror channel.

Reward-schedule authority stays in Python: `task_context.py:726-728` advances
`reward_schedule['index']` after each trial, so the delegate resolves durations via
`get_reward` (`task_context.py:784`) and passes them in `TrialConfig`; Rust only
*shapes* the `inject_analog` pulse (`reward.rs`), it does not own the schedule.

### Process / stream diagram

```
                localhost:50050  (C++ thalamus gRPC server, insecure)
  ┌──────────────────────────────────────────────────────────────────────┐
  │  Thalamus:  analog · log · inject_analog · ping                        │
  └──────────────────────────────────────────────────────────────────────┘
       ▲ analog(X,Y)   ▲ log(BehavState)   ▲ inject_analog(Reward)
       │  stream        │                   │
  ┌────┴────────────────┴───────────────────┴───────────────────────────────┐
  │                       RUST PROCESS  (Linux rig)                          │
  │  grpc.rs · input.rs · state.rs · render/ (240Hz) · reward.rs · mirror.rs │
  │  hosts RustTask @ localhost:50060                                        │
  └───────────────▲───────────────────────────────────────┬──────────────────┘
                  │ run_trial(TrialConfig)                 │ TrialEvent stream
                  │ (config_json + reward_ms +             │ (BehavMarker, behav_result_json,
                  │  reward_scale + python_perf_ns)        │  success) + MirrorFrame @30Hz
  ┌───────────────┴──────────────────────────────────────────▼────────────────┐
  │  PYTHON task_controller (operator front-end)                              │
  │  joystick_intro_rust.py delegate.run(context)  [if self.widget: branch]   │
  │  TaskContext.run: TRIAL START/FINISHED + trial_summ (unchanged)           │
  └────────────────────────────────────────────────────────────────────────────┘
```

## Rendering stack (240 Hz Linux)

- `winit` + `wgpu` (Vulkan). Exposes explicit present-mode control (the biggest
  latency lever); works on X11 and Wayland. SDL2 is a fallback (repo has
  `src/sdl2-config.cpp`); `minifb`/`pixels` fine only for the M1 spike.
- **Exclusive/unredirected fullscreen** on the subject monitor so the compositor is
  bypassed (~1 frame instead of ~2). X11 unredirected fullscreen or Wayland direct
  scanout.
- Present mode `Fifo` (hard vsync, no tearing) for the production photodiode-faithful
  path; expose `Immediate`/`Mailbox` behind a flag to measure the tear-allowed floor.
- **Frame-paced loop, input sampled last**: each vsync — read newest joystick sample,
  step the state machine by measured `dt`, record the draw, `submit` + `present`.
  Never let input or state logic block the present.
- **Photodiode square**: grayscale `state_brightness` fill in the corner, size 70 px,
  offset by `state_indicator_x`/`_y` (`joystick_intro.py:2828-2836`), toggled exactly
  on `BehavState` transitions (`:3081`). It is the measurement instrument — keep it
  pixel/timing faithful.

Expected: command->photon ≈ 1 present interval + scanout ≈ ~4-6 ms (~1-1.5 frames)
at 240 Hz, vs ~50 ms today.

## Clock synchronization

Python logs `int(perf_counter()*1e9)` (`task_context.py:409`) with no server sync.
Rust must land `BehavState` `Text.time` in that same domain so markers fall between
Python's TRIAL START/FINISHED. Seed: `TrialConfig.python_perf_ns` captured right
before `run_trial`; Rust records its `steady_clock` on receipt; the difference is a
fixed offset (`clock.rs`). Refine with a short ping/pong (min-RTT) to remove one-way
call bias. Independently, use `Thalamus.ping` + `Pong.remote_time` and `SyncNode`
(`src/sync_node.cpp`) for photodiode alignment during latency measurement.

## Milestones

- **M0** Baseline: record current ~50 ms photodiode distribution.
- **M1** Latency floor: minimal wgpu fullscreen photodiode square; measure
  command->photon across present modes and X11 vs Wayland. Establishes the floor
  before any task logic.
- **M2** Input: consume `analog(X,Y)`; render tracking cursor; measure input->photon.
- **M3** State machine + logging + reward: port `joystick_intro.py:2881-3235` into
  `state.rs`; fill `events.rs` (behav_result); BehavState markers to `Thalamus.log`;
  rewards via `inject_analog`. Wire the delegate + register in `tasks.py`.
- **M4** Operator mirror: `RustTask.frames`; delegate paints frames on the operator
  Canvas at 30 Hz. Re-measure latency to confirm no subject-path regression.
- **M5** Parity validation: identical configs through Python and Rust; diff capture
  files + behavior.

## Parity / verification

- **Latency**: photodiode command->photon per milestone vs the M0 baseline;
  cross-check with `SyncNode`. Target ~1-1.5 frames.
- **Behavioral parity**: feed the same `TaskConfig` JSON + a recorded joystick input
  trace to both implementations; assert identical `BehavState`/bracket sequences,
  matching reward pulses, and (after `json.loads` normalization) identical
  `behav_result`. Diff `StorageRecord` capture files with `thalamus/record_reader.py`.
- **behav_result byte-rules**: match Python dict key *order* (serde serializes struct
  fields in declaration order — `events.rs` field order mirrors Python) and emit
  `null` for `None` (Option without skip). Numeric types: `int` vs `float` as Python.
- **Clock**: verify Rust-emitted `Text.time` interleave correctly and land within the
  trial window.
- **Sounds** (success/fail clips, `joystick_intro.py:2037-2038`) stay Python-side,
  triggered by the delegate on the corresponding `TrialEvent`, to avoid porting audio.

## Risks / mitigations

- **Config/semantics drift** (two task definitions): `config.rs` tracks every key +
  default from `joystick_intro.py`; golden-config parity tests (M5) diff logs/outcomes;
  treat `joystick_intro.py` as the spec of record.
- **Clock skew**: min-RTT ping refinement; assert markers land within the trial window.
- **behav_result key order / numeric types**: ordered structs + golden JSON diff.
- **Reward authority**: Python resolves `reward_ms`; Rust only shapes the pulse.
- **Wayland vs X11 present variance**: M1 measures both; pin the rig to the
  lower-latency path; keep a present-mode flag.
- **Mirror stealing GPU/CPU from the subject present**: separate thread, capped
  30 Hz, downscaled, off the present-critical path; M4 re-measures.
- **Maintaining two implementations**: keep `state.rs` a strict structural mirror of
  the Python `run`; golden-config regression suite in CI.

## Key files

- Task spec of record: `thalamus/task_controller/joystick_intro.py` — `run` @2034,
  config keys @2056-2113, `renderer` @2694, photodiode square @2828, joystick stream
  @2848, cursor integration @2886, events @2409-2437, reward @2342-2365.
- Control seam analysis: `thalamus/task_controller/task_context.py:696-746` (branch),
  `:406-417` (log/behav_result), `:784` (get_reward); `thalamus/task_controller/servicer.py:77`.
- Byte-compat enabler: `src/storage2_node.cpp:146-163` (`on_log`, empty `node`).
- IPC contracts (canonical, in `proto/`): `proto/thalamus.proto` (`analog`, `log`,
  `inject_analog`, `ping`), `proto/task_controller.proto`, `proto/util.proto`, and the
  new `proto/rust_task.proto` (delegate <-> Rust). `thalamus/*.proto` are generated
  copies. Python stubs: `thalamus/build.py` (`rust_task` is in its `services` list).
  Rust stubs: `rust/joystick_task/build.rs` (reads `../../proto`).
- Rust scaffold: `rust/joystick_task/` (see `rust/README.md` for the per-file status).
- Python delegate: `thalamus/task_controller/joystick_intro_rust.py`.
```
