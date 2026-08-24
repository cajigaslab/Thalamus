# Changelog

This changelog summarizes notable, user-facing changes per release.  It is curated
from the project's commit history; the canonical list of releases and downloadable
wheels lives on the [Releases page](https://github.com/cajigaslab/Thalamus/releases).

Thalamus uses `MAJOR.MINOR.PATCH` versions.  Releases are produced automatically, so
some patch versions contain only build/CI or internal changes and are omitted below.

## 1.0.x

### 1.0.38–1.0.40 — 2026-08
- Build fixes only: renamed the bundled `glslangValidator` executable asset to
  `glslang` and fixed `GLSLANG_ASSET` naming/formatting for macOS.

### 1.0.37 — 2026-07-31
- OCULOMATIC recenter requests are now written to the pipeline log as
  `Oculomatic Recenter` events, so recordings capture when the operator recentered.

### 1.0.36 — 2026-07-24
- Redesigned the Vulkan queue API and exposed Vulkan to native plugins through
  `plugin.h`: `get_vulkan_instance/device/physical_device/queue`,
  `create_vulkan_command_pool`, and `lock_vulkan_queue`/`unlock_vulkan_queue` for
  safe submission on the shared queue.
- ImageViewer: SDL polling refactored into a single static loop, `SDL_Quit`
  deferred to program shutdown, and `VK_KHR_portability_enumeration` used when
  available (macOS/MoltenVK).

### 1.0.35 — 2026-07-24
- Plugin API: the load symbol is now `thalamus_get_node_factories` (was
  `get_node_factories`), and an optional `thalamus_teardown` export is called at
  shutdown so plugins can release their resources.
- Fixed a hang on shutdown when Thalamus was started with an empty pipeline.

### 1.0.34 — 2026-07-22
- Fixed the SpikeGLX node: streaming ran concurrently with heartbeats, causing
  NOOP and FETCH responses to intermingle and be read incorrectly; shutdown no
  longer hangs Thalamus.
- macOS: Vulkan build fixes (QuartzCore framework dependency, loader target).

### 1.0.31–1.0.33 — 2026-07
- `ThalamusAPI` is now versioned: the table begins with a `version` field and each
  function is annotated with the version that introduced it, so plugins can guard
  newer capabilities.
- Added gRPC reflection to the Python service.
- Thread-safety hardening: fixed data races found by ThreadSanitizer, an
  `inject_analog` data race, and synchronized plugin node reference counting; new
  build/run flags can disable Vulkan and Crashpad.
- Platform-specific Vulkan loader library paths.

### 1.0.30 — 2026-07-09
- Added `--wait-for-pipeline` to the task controller and pipeline entry points:
  attach to an externally launched pipeline instead of starting one.
- PUPIL node: new **Frequency** (frame rate) and **Jitter (Pixels)** parameters for
  simulating camera rates and tracking noise.
- Task controller: `wait_for_hold` gained `blink_resets` (a blink restarts the
  hold timer, for paradigms requiring continuous fixation); fixed a buggy
  `wait_for`.
- `inject_analog` now buffers data that arrives before the target node is found
  and delivers it once the node appears.

### 1.0.26–1.0.28 — 2026-06/07
- Merged new input nodes: joystick, serial touch screen, and an improved ArUco
  tracking node.
- Extension/plugin API: image-data subscriptions and mocap publishing for
  extension nodes, plus an image extension-node demo (`BallNode`); removed C
  strings from the plugin interface in favor of spans.
- TOUCH_SCREEN: new **Null Threshold** parameter — raw coordinates below it are
  treated as "no touch" and passed through untransformed.
- Task controller: blink and `wait_for` timeouts may now be `None`; improved
  remote-executor support and command-line interface.

### 1.0.17–1.0.25 — 2026-06
- STORAGE and STORAGE2 now enable video compression by default.
- Eye calibration: eye opacity slider, target grid and clearer target-selection
  highlighting, target presets, a reset button, nudges/notches persisted across
  refits, and a more compact UI.
- Task controller: task error handling and an error message when a config
  references an undefined task; *Save As* aborts cleanly if no file name is
  chosen.
- Python packaging now depends on pandas and pyarrow (for `thalamus.dataframe`).

### 1.0.16 — 2026-06-08
- Eye calibration: finished polar interpolation in Angular Scaling mode, added a
  default scaling parameter, tooltips, and a reward node with hold parameters;
  further undo/redo improvements.
- Plugins can now read analog data from other nodes, inject analog with a callback
  interface, and pass requests through to other nodes (e.g. OCULOMATIC device
  queries) without blocking shutdown.
- Fixed the task-controller undo/redo stack being lost on *Clear*, and the saving of
  decorated task source (now resolved with ``functools.wraps`` / ``inspect.unwrap``).
- ``ObservableCollection`` fixes (parent detachment on replacement; insert no longer
  overwrites following values).
- Documentation: full visual redesign, the official brain/circuit logo, and the
  node/example coverage from the 1.0.15 docs overhaul.

### 1.0.15 — 2026-06-02
- Eye calibration: added undo/redo.
- Fixed an OCULOMATIC crash when the X/Y gain was a floating-point value.
- Added a `registry` module for editing Thalamus state.

### 1.0.14 — 2026-06-01
- Recordings now save the build type, git commit, and git version tag; each task is
  copied into the output directory the first time it executes in a recording.
- Eye calibration moved into its own module, with point nudging and pinned-point
  drawing in the angular-scaling view.
- Angular scaling can now scale X and Y independently, with a crosshair overlay.
- Added a projective-model calibrator and a code switch to toggle between the
  OpenGL and regular Qt task widgets.
- OCULOMATIC now tracks the pupil with doubles instead of ints.
- Plugins can now read analog data from other nodes.
- Fixed a duplicate-observer bug that could cause clients to receive duplicate
  delete events, and fixed restoring the pipeline/task-controller window geometry.

### 1.0.12 — 2026-05-18
- `.NET` bindings are now built in CI.
- The NIDAQ integration loads any version of `libnidaqmx.so`.
- `DataFrameBuilder` gained an option to warn (instead of raising) on inconsistent
  sample intervals.

### 1.0.5 — 2026-05-04
- Added an HTTP 1.1 + WebSocket interface.
- Added Emscripten (WebAssembly) build support.
- Added node sorting and filtering in the UI.
- Added the SAMPLE_MONITOR node (with a configurable interval) for watching node
  sample rates.
- Added an "About Thalamus" dialog showing build info, and a sequence number to
  `StorageRecord`.
- Added the `thalamus.video_writer` module and `get_paths` on `MultiVideoReader`.
- Added the CECI stimulation extension.
- Added pthread policy/priority configuration on Linux.

### 1.0.1 — 2026-04-07
- Rewrote and hardened the Rust API (pattern-matching state API, generic
  `on_change` callbacks, safer plugin surface).
- Plugin multithreading; plugin data exchange now uses pointers, with error-message
  access from plugins.
- Added a Go node demo and finished the `EXT_SERIAL` node (reads from a pty).
- Fixed PyQt6 compatibility issues and a freeze when creating a log stream.
- `VideoWriter` can use a custom FFmpeg invocation.

### 1.0.6–1.0.11, 1.0.13 — 2026-05
- Build, packaging, and CI fixes (Windows/macOS/Linux toolchains, clang/LLVM
  version handling, CMake Python-executable option, `asyncio.run` startup).

## 0.3.x and earlier

The 0.3.x series and earlier predate this curated changelog.  See the
[Releases page](https://github.com/cajigaslab/Thalamus/releases) for the full tag
history and downloadable builds.
