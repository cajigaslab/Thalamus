# Rust low-latency task executor (real-time BCI patch)

Standalone Rust process that renders the `joystick_intro` behavioral task with
command-to-photon latency near one display frame (~4-6 ms on a 240 Hz Linux
subject display), replacing the ~50 ms of the Python/Qt render path. It integrates
with the existing Thalamus core purely over gRPC — no changes to the C++ core or
Python packaging.

**Read `docs/rust_bci_patch.md` first** for the architecture and rationale. This
README is the build/orientation/status companion.

## Why this exists

The ~50 ms is architectural, not "Python is slow": a 10 ms task-loop sleep, an
unsynchronized ~16 ms Qt/asyncio super-loop tick, a `QOpenGLWidget` FBO composite
(+1 frame), and vsync (+1 frame) stack up. A Rust process that owns a tight
vsync-paced loop (sample input -> step state -> render -> present) on an
unredirected Linux fullscreen surface collapses these into ~1-1.5 frames.

## Layout

The delegate <-> Rust control contract lives in the repo's canonical proto dir:
`proto/rust_task.proto` (compiled by BOTH `build.rs` for Rust and `thalamus/build.py`
for the Python stubs — single source of truth, no forked copy).

```
rust/joystick_task/
  Cargo.toml            deps (render deps commented out until M1)
  build.rs              tonic-build: compiles ../../proto/{thalamus,task_controller,util,rust_task}.proto
  src/
    main.rs             entrypoint: hosts RustTask (:50060), connects Thalamus (:50050)
    proto.rs            generated-module re-exports
    config.rs           TaskConfig — mirrors every joystick_intro.py config key   [COMPLETE]
    constants.rs        per-target defaults from joystick_intro.py:41-49           [COMPLETE]
    clock.rs            steady_clock -> Python perf_counter mapping                [COMPLETE]
    grpc.rs             Thalamus client: analog / log / inject_analog              [COMPLETE]
    input.rs            joystick analog stream -> lock-free latest (X,Y)           [COMPLETE]
    reward.rs           inject_analog reward pulse (byte-faithful)                 [COMPLETE]
    events.rs           behav_result data model (byte-order parity)               [MODEL ONLY]
    state.rs            joystick_intro state machine port                          [STUB — M3]
    render/mod.rs       winit+wgpu frame-paced render loop                        [STUB — M1/M3]
    mirror.rs           operator live-mirror (JPEG @30Hz)                          [STUB — M4]
    control.rs          RustTask service impl (run_trial/frames)                  [SCAFFOLD]
```

Python side: `thalamus/task_controller/joystick_intro_rust.py` (delegate;
reuses `joystick_intro.create_widget`, forwards to Rust). Not yet registered in
`tasks.py` — see "Wiring in" below.

## Prerequisites

- Rust toolchain (`rustup`, stable). Not installed on the macOS dev machine; the
  real build/measurement happens on the Linux rig.
- `protoc` (protobuf compiler) on PATH, for the Rust build. macOS: `brew install
  protobuf`; Debian/Ubuntu rig: `apt-get install protobuf-compiler`.
- `grpcio-tools` (Python), for the Python stub generation: `pip install grpcio-tools`.
- For M1+ rendering: Linux with Vulkan drivers and X11/Wayland dev headers.

## Build & test

```sh
cd rust/joystick_task
cargo build            # compiles proto codegen + foundation modules
cargo test             # unit tests: config defaults, analog parsing, reward shape, clock
```

The first build intentionally excludes the render stack (winit/wgpu deps are
commented out in `Cargo.toml`) so it is fast and cross-platform. `cargo test`
exercises the machine-independent foundation and validates the proto codegen.

## Run

The executor is auto-spawned by task_controller's Orchestration
(`eevee.json` `Orchestration.Processes` runs the release binary with
`--thalamus http://127.0.0.1:50050`; stdout lands in the thalamus terminal,
process is killed on shutdown, non-critical so an executor crash fails trials
loudly but does not end the session). So the normal flow is just:

```sh
# 1. Build once after any Rust change:
cd rust/joystick_task && cargo build --release
# 2. Start thalamus as usual (spawns core + executor):
python -m thalamus.task_controller --config eevee.json
# 3. Select the "Joystick Intro (Rust)" task.
```

To run the executor by hand instead (debugging), remove/disable the
Orchestration entry and:

```sh
cargo run --release -- --thalamus http://127.0.0.1:50050 --listen 127.0.0.1:50060
```

Note: only one executor can bind :50060 — if an orphaned one is still running,
the spawned one exits immediately (check the thalamus log). The executor grabs
monitor 1 fullscreen (override-redirect) from startup, showing the idle scene.

Shutdown (2026-07-02): the executor handles SIGINT/SIGTERM by exiting the
render loop between frames — window, swapchain, and core connections tear
down cleanly, with a 2 s hard-exit fallback (main.rs signal task). The
Orchestrator likewise SIGTERMs before SIGKILLing (orchestration.py). This was
added after a Ctrl+C of the orchestrated session froze the desktop: journal
forensics showed kernel/GPU/X all healthy, so the suspect is abrupt
termination (default SIGINT + orchestrator SIGKILL) mid-frame/mid-stream —
either the flipped fullscreen surface dying uncleanly or the core wedging on
abruptly-dropped client streams while Python awaited it with dead Qt windows
covering the monitors. If a freeze recurs, get in via ssh/TTY (Ctrl+Alt+F3)
and check for a hung `python -m thalamus.task_controller` / native core
before rebooting.

## Wiring in (when ready)

1. Generate the Python stubs. `rust_task` is already registered in
   `thalamus/build.py`'s `services` list, so the normal package build/generate
   emits `thalamus/rust_task_pb2.py` + `_pb2_grpc.py` (with relative imports) from
   `proto/rust_task.proto`. To generate on demand without a full build, run from
   the repo root the same command `build.py` uses:
   ```sh
   python -m grpc_tools.protoc -Iproto \
     --python_out=thalamus --grpc_python_out=thalamus --pyi_out=thalamus \
     proto/rust_task.proto
   # build.py normally rewrites the grpc import to relative form; if you ran the
   # raw command above, edit thalamus/rust_task_pb2_grpc.py:
   #   'import rust_task_pb2 as ...' -> 'from . import rust_task_pb2 as ...'
   ```
   The delegate imports these lazily (`from .. import rust_task_pb2`), so nothing
   breaks before they exist.
2. Register the delegate in `thalamus/task_controller/tasks.py`:
   ```python
   from . import joystick_intro_rust
   TaskDescription('joystick_intro_rust', 'Joystick Intro (Rust)',
     joystick_intro_rust.create_widget, joystick_intro_rust.run),
   ```
   Until then, selecting it is impossible and nothing is disturbed.

## Milestones (status)

| # | Goal | Status |
|---|------|--------|
| M0 | Baseline: record current ~50 ms photodiode distribution | rig task |
| M1 | Latency floor: wgpu fullscreen photodiode square, measure command->photon | DONE 2026-07-01 — paced fifo p50 11.3 ms (see below) |
| M2 | Input: consume analog stream, render tracking cursor | DONE (control.rs pump_input + render loop) |
| M3 | State machine + logging + reward: port joystick_intro.py:2881-3235 | DONE 2026-07-01 — full port, 29 unit tests, e2e smoke green |
| M4 | Control handoff + operator mirror | DONE 2026-07-02 — mirror streams shape-list re-rasterized JPEGs @30 Hz; delegate paints them on the operator Canvas |
| M5 | Parity validation vs Python (capture diff) | waived 2026-07-02 — full live training session ran clean; formal capture diff skipped by choice |

**Live production result (2026-07-02):** a full training session through the
Rust path measured a **median command-to-photon photodiode response of
16.8 ms**, vs the ~50 ms median of the Python/Qt path — a ~3x improvement,
consistent with the M1 spike (11.3 ms p50 for bare rendering; the extra ~5 ms
is the full state machine + input path in the loop).

## Foundation that is DONE and testable now

- Proto codegen from the real contracts (`build.rs`, `proto.rs`).
- `config.rs`: full `TaskConfig` with defaults matching joystick_intro.py, unit-tested.
- `grpc.rs`: Thalamus client wrappers for analog/log/inject_analog.
- `input.rs`: analog X/Y parsing (matches `analog_processor`), lock-free latest sample, unit-tested.
- `reward.rs`: byte-faithful reward pulse, unit-tested.
- `clock.rs`: Python-perf_counter mapping, unit-tested.
- `events.rs`: `behav_result` struct model with key-order/`null` parity rules.
- `control.rs` + `main.rs`: RustTask server hosting + config parse + clock seed.

## M1 spike usage (code done 2026-07-01; measurement is a rig task)

Toolchain is installed on the Linux rig (rustup stable + protoc 29.3 in
`~/.local/bin`); `cargo test` green (10/10). The spike renders fullscreen,
toggles the photodiode square (70x70, same bottom-right spot/margins as the
Python task) every N frames, and writes each toggle's command timestamp
(CLOCK_MONOTONIC ns) to CSV:

```sh
cargo run --release -- spike --present-mode fifo --seconds 30 --csv fifo.csv
# repeat with --present-mode mailbox / immediate; Esc or q quits early.
# --monitor N picks the subject display; --windowed for debugging.
```

Command-to-photon is computed AUTOMATICALLY when the Thalamus core is running:
the spike streams the photodiode channel (NIDAQ "Analog in" Dev1/ai1, exposed
as "Node 5"/"Photodiode" — override with --diode-node/--diode-channel) during
the run, detects edges, and prints the latency distribution at exit, plus a
`<name>_latency.csv`. This works because `AnalogResponse.time` is C++
steady_clock (grpc_impl.cpp:353) == CLOCK_MONOTONIC == the spike's `t_cmd_ns`
domain; diode sampling is 1 kHz so keep the toggle period >= 50 ms (the
analyzer refuses shorter). For `immediate` runs use `--toggle-frames` big
enough (e.g. 2000 at ~4000 fps). Without the core, runs degrade to CSV-only
(`--no-diode` silences the warning).

Rig facts: subject display = HDMI-0, 1920x1080 @ 240 Hz = `--monitor 1`
(`--list-monitors` prints the table). Windowed smoke test: 239.67 fps,
frame-interval p50 4.166 ms — vsync-locked. `immediate` fullscreen ran
~4000 fps, so the GPU pipeline is far from the bottleneck.

### M1 RESULTS (measured 2026-07-01, photodiode via NIDAQ @1 kHz)

| configuration | p50 ms | p90 ms | max ms | what it isolates |
|---|---|---|---|---|
| Python/Qt task (M0, doc baseline) | ~50 | | | current production path |
| fifo, WM fullscreen (composited) | 40.3 | 48.3 | 53.0 | Mutter X11 composites ALL monitors on the 60 Hz primary's clock |
| fifo, `--override-redirect` | 29.7 | 31.7 | 32.3 | compositor bypassed; NVIDIA fifo queue ~5 frames deep remains |
| fifo, o-r + `--pace-margin-ms 1.5` | **11.3** | 12.3 | 16.5 | production candidate: vsync, no tearing, queue ~1 |
| immediate, `--override-redirect` | 8.6 | 11.2 | 12.5 | the physical floor (scanline + LCD + monitor lag) |

Hard-won lessons baked into the code (spike.rs):
- `_NET_WM_BYPASS_COMPOSITOR=1` alone did NOT make Mutter unredirect the
  wgpu window; a `--override-redirect` (unmanaged) window did. The production
  M3 window must be override-redirect (keyboard input then needs explicit
  handling — winit gets no focus).
- NVIDIA X11 Vulkan ignores `desired_maximum_frame_latency=1`; free-running
  fifo saturates a ~5-frame queue (~20 ms). Frame pacing must (a) stamp
  decisions AFTER the blocking acquire, (b) one-shot drain the queue (a fifo
  queue drains 1/vblank — pacing alone never shrinks it), (c) roll the sleep
  over to the NEXT vblank when already inside the margin window, else it
  submits twice per refresh and silently refills the queue.
- `mailbox` is not supported by this driver (fifo/fifoRelaxed/immediate only).
- LCD asymmetry: black->white ~2 ms slower than white->black at mid-swing.
- Remaining ~8 ms floor is display physics + ~1-2 ms diode-chain uncertainty,
  not software.

## M3 status (DONE 2026-07-01)

The full trial pipeline works end to end:

- `state.rs`: faithful port of run() @2034-3246 — cursor integration (direct/
  cumulative/zero-drift/influence/operator latch), ITI + center gate, hold/
  entry/timeout/ignored-idle, free-play (bout/first-touch/sustain rewards),
  touch fail, streak + bonus, all ~19 append_event sites. NOT ported: the
  success pop/particle stall (@3167-3188, animations-off by default) and
  on-screen text (glyphon lands with the M4 mirror).
- `events.rs`: behav_result with Python-exact key order (verified by unit test
  and e2e), mode-dependent key presence (free-play block, finalize tail).
- `run_trial` is now BIDIRECTIONAL: first message TrialConfig, then
  OperatorEvents (arrow keys / free-play end / touch) forwarded by the
  delegate; TrialEvent gained config_updates_json (_streak_count,
  _last_cursor_x/y, _operator_keys_pressed persistence).
- Executor threading: main thread = winit render loop (override-redirect,
  paced fifo — the M1 recipe); background tokio runtime = tonic server +
  Thalamus client (input pump / BehavState log / reward injection).
- Python: stubs generated (`thalamus/rust_task_pb2*`), delegate rewritten
  (operator-event forwarding, sounds on success/fail markers, config updates),
  REGISTERED in tasks.py as "Joystick Intro (Rust)".
- Trial start latency ~4 ms. Gotcha found: the C++ core takes ~1 s to answer
  an analog subscribe, so the executor must never await stream-open before
  starting the trial (Python never awaited it either). Use 127.0.0.1 rather
  than localhost in endpoints to dodge a 1 s IPv6 fallback in gRPC.
- Live-session bug pair (2026-07-02): (a) tonic client-streaming calls (log /
  inject_analog) resolve only when the request stream ENDS, so awaiting them
  inline deadlocked the effects task before a single message went out — no
  BehavState logs or reward pulses reached the core while trials otherwise ran
  normally. grpc.rs now spawns the RPC future (Python-equivalent un-awaited
  `stub.inject_analog(queue)`) and reopens broken streams. (b) The delegate's
  request iterator must be an IterableQueue, not an async generator — grpc.aio
  tears the iterator down from another task at RPC end, raising "aclose():
  asynchronous generator is already running" and killing task_controller (and
  the core it spawned).
- Real-config gotcha (crashed the first live attempt): the Qt Form UI stores
  int-like values as floats ("reward_channel": 2.0), which Python reads
  through int() casts. config.rs now parses ALL numeric/bool fields leniently
  (mod lenient) with a regression test against the eevee.json shape. Related:
  when the executor rejects a config, a gRPC client-side race can mask the
  status as INTERNAL — the executor's terminal logs the real reason, and the
  delegate now fails the trial instead of crashing task_controller.

Run it: `cargo run --release -- --thalamus http://127.0.0.1:50050` (defaults:
subject display --monitor 1, paced fifo). Then select "Joystick Intro (Rust)"
in task_controller.

## M4 mirror (DONE 2026-07-02)

No GPU readback: the render thread publishes each frame's SHAPE LIST (a few
hundred bytes) into a watch channel; frames() re-rasterizes it on the CPU at
mirror resolution (mirror.rs, coverage rules identical to the WGSL) and
streams JPEGs (pure-Rust jpeg-encoder; 480x270 @ 30 Hz measured, ~2.7 KB/frame
idle). The subject present path is untouched. The delegate opens the stream
per trial and paints frames aspect-fit on the operator Canvas. Between trials
the mirror correctly shows the idle scene (black + dark photodiode square).
HUD text (target name / next target / reward channel) is still TODO.

## Next actions for a future session

Nothing blocking — the patch is in production (see live result above; the
executor is auto-spawned via Orchestration). Deliberately deferred:

1. Operator HUD text (target name/size/reward-channel/next-target) and
   subject-side status text (glyphon) — dropped 2026-07-02, operator doesn't
   need them.
2. Formal M5 capture diff with thalamus/record_reader.py — waived; the parity
   unit tests in events.rs/state.rs plus a clean live session were judged
   sufficient. Revisit if analysis scripts flag anything odd in the records.

## Future avenue: built-in remote-executor seam (evaluated 2026-07-02, deferred)

Thalamus already has a generic executor seam, used by cajigaslab's
cpp-executor / godot-executor (and py_executor): start task_controller with
`--remote-executor`, and the executor process dials task_controller's gRPC
port (50051) and opens the bidi `execution` stream — per trial it receives
`TaskConfig{body: <task_config json>}` and replies `TaskResult{success: bool}`
(proto/task_controller.proto; served by task_controller/servicer.py:77-97;
dispatched at task_context.py:696-722). We considered restructuring this Rust
executor to use that seam instead of the custom RustTask delegate and decided
against it FOR THIS RIG, because today the seam:

- returns only `bool success` — no channel for behav_result, BehavState
  markers, or config_updates (streak counts), so trial_summ in the capture
  file loses the behavioral record; TRIAL START/FINISHED are also only logged
  in the widget branch (task_context.py:701-703) and would vanish;
- requires `--remote-executor`, which skips creating the subject
  Window/Canvas (main_impl.py:198-213) — no operator mirror surface, no
  arrow-key/end-key/touch forwarding, no sounds;
- has a single executor slot that receives EVERY queued task type, so a mixed
  queue (Rust joystick_intro + Qt tasks) is impossible;
- Python's reward-schedule resolution (context.get_reward) is bypassed and
  would need reimplementing in the executor.

Revisit when: the rig moves to a dedicated headless subject machine running
ALL tasks in one executor (task_controller headless, operator UI elsewhere).
Prerequisite: extend the upstream TaskController proto so TaskResult carries
behav_result/markers/config_updates. Worth borrowing sooner regardless: the
executor-dials-controller direction (free reconnection; Python never needs to
know the executor address) could be adopted by the RustTask proto without
giving up the rich delegate channel.
