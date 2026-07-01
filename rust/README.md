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

## Run (once M3/M4 land)

```sh
# 1. Thalamus core running on :50050 (as usual).
# 2. Start the executor (long-lived):
cargo run --release -- --thalamus http://localhost:50050 --listen 127.0.0.1:50060
# 3. In task_controller, select the "Joystick Intro (Rust)" task.
```

Eventually `main_impl.py` should spawn the executor the way it spawns the native
binary (main_impl.py:170), passing `--thalamus`/`--listen`.

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
| M1 | Latency floor: wgpu fullscreen photodiode square, measure command->photon | not started (needs rig) |
| M2 | Input: consume analog stream, render tracking cursor | foundation ready (`grpc.rs`, `input.rs`) |
| M3 | State machine + logging + reward: port joystick_intro.py:2881-3235 | `state.rs`/`events.rs` stubbed |
| M4 | Control handoff + operator mirror | `control.rs`/`mirror.rs` scaffolded |
| M5 | Parity validation vs Python (capture diff) | not started |

## Foundation that is DONE and testable now

- Proto codegen from the real contracts (`build.rs`, `proto.rs`).
- `config.rs`: full `TaskConfig` with defaults matching joystick_intro.py, unit-tested.
- `grpc.rs`: Thalamus client wrappers for analog/log/inject_analog.
- `input.rs`: analog X/Y parsing (matches `analog_processor`), lock-free latest sample, unit-tested.
- `reward.rs`: byte-faithful reward pulse, unit-tested.
- `clock.rs`: Python-perf_counter mapping, unit-tested.
- `events.rs`: `behav_result` struct model with key-order/`null` parity rules.
- `control.rs` + `main.rs`: RustTask server hosting + config parse + clock seed.

## Next actions for a future session

1. On the rig: install toolchain + protoc, `cargo test` (green = codegen + foundation OK).
2. M1 spike: uncomment render deps, implement `render/mod.rs` minimal window +
   photodiode square, measure the latency floor across present modes and X11 vs
   Wayland. This number drives everything else.
3. M3: port the state machine into `state.rs`, filling `events.rs` per the ~19
   `append_event` call sites, wiring `grpc`/`input`/`reward`.
4. M4/M5: mirror + golden-config parity tests (diff capture files with
   `thalamus/record_reader.py`).
```
