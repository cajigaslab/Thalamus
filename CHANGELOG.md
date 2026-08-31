# Changelog

This changelog summarizes notable, user-facing changes per release.  It is curated
from the project's commit history; the canonical list of releases and downloadable
wheels lives on the [Releases page](https://github.com/cajigaslab/Thalamus/releases).

Thalamus uses `MAJOR.MINOR.PATCH` versions.  Releases are produced automatically, so
some patch versions contain only build/CI or internal changes and are omitted below.

## 1.0.x

### 1.0.42 — 2026-08-28
- Fixed a bug where cancelling a task running on a remote executor (`--remote-executor`)
  didn't actually stop it: cancellation now notifies the remote executor so it stops
  the in-flight task instead of continuing to run in the background.
- Fixed STORAGE2 corruption when recording planar image formats (`YUYV422`,
  `YUV420P`, `YUVJ420P`, `RGB16`) — the chroma/luma byte strides were wrong for
  some formats.
- `thalamus.dataframe` no longer emits empty columns for channels that received
  no data during a recording.
- macOS: fixed the packaged distribution failing to find a Vulkan driver by
  bundling MoltenVK's ICD alongside the app and pointing the loader at it.

### 1.0.41 — 2026-08-24
- **Behavior change:** the task controller's gRPC servers (and the native
  pipeline process) now bind to `127.0.0.1` only by default. Pass `--open` to
  bind `0.0.0.0` as before — for example when a `REMOTE`/`RUNNER2` node on
  another machine needs to reach this instance.
- Windows: the task controller now checks that the .NET 8 ASP.NET Core runtime
  is installed before launching the C# sidecar process, showing a one-time
  install prompt instead of crashing when it's missing.
- Plugin API: added functions to build detached state values
  (`state_make_dict`/`state_make_list`), write state/list entries with a
  completion callback, and open/drive an SDL window from a plugin.
- OCULOMATIC's rendering was rewritten in C++ so it stays live through UI
  stalls, and the image viewer gained RGB source support alongside grayscale.

### 1.0.37 — 2026-07-31
- OCULOMATIC recenter requests are now written to the pipeline log as
  `Oculomatic Recenter` events, so recordings capture exactly when the operator
  recentered during a session.

### 1.0.36 — 2026-07-27
- Plugins can now use Vulkan directly: the C API exposes the host's Vulkan
  instance/device/queue (with a lock around queue submission) so a plugin can
  render into its own surface.
- Misc Vulkan/SDL fixes in the image viewer (a `VK_KHR_portability_enumeration`
  fallback, SDL event polling consolidated into a single loop, deferred
  `SDL_Quit` until shutdown).

### 1.0.35 — 2026-07-24
- **Breaking (plugins):** a plugin's node-factory entry point must now be
  exported as `thalamus_get_node_factories` (previously `get_node_factories`);
  a plugin still exporting the old name will fail to load.
- Added an optional `thalamus_teardown` library-level hook plugins can export
  for cleanup at pipeline shutdown.
- Fixed a shutdown hang when the pipeline had zero live nodes.

### 1.0.34 — 2026-07-24
- Fixed a SpikeGLX node bug where streaming ran concurrently with heartbeats,
  causing NOOP/FETCH responses to become intermingled and misread; also fixed
  a shutdown hang.
- macOS: fixed a missing QuartzCore framework dependency for the Vulkan
  loader.

### 1.0.32 — 2026-07-20
- `python -m thalamus.pipeline` gained `--no-gpu` to disable GPU/Vulkan
  rendering (falls back to software rendering).
- The pipeline's gRPC server now has reflection enabled, so generic clients
  (`grpcurl`, `grpcui`) can introspect it without the `.proto` files.
- `ThalamusAPI` gained a `version` field plugins can check for capability
  availability.
- Plugin nodes can now register an optional `predrop` hook to run
  asynchronous cleanup before being torn down or replaced (signaling
  completion via `node_predrop_ready`).
- Fixed `connect channels changed` to be a no-op on non-analog nodes.

### 1.0.31 — 2026-07-15
- Added `--wait-for-pipeline` to `thalamus.task_controller`, to wait for
  something else to launch the data pipeline instead of spawning it itself.
- PUPIL node: added `Frequency` (frame rate) and `Jitter (Pixels)` parameters.
- `wait_for_hold` gained a `blink_resets` option (restart the full hold
  duration on any blink); fixed a race in `wait_for` where it could return a
  stale re-evaluation of its condition instead of the value that actually
  satisfied the wait.
- Fixed analog data injected before its target node exists being dropped
  instead of buffered and delivered once the node appears.

### 1.0.28 — 2026-07-02
- ARUCO: added multi-camera fusion (`Sources`), a `layout` board type for
  markers that aren't on a regular grid, per-board quality metrics, an
  annotated debug image, and a wand-calibration mode.
- `thalamus.record_reader2` gained real argument parsing (`-n/--node` filter,
  `-s/--stats` for a live count summary instead of per-record printing);
  added a `thalamus.video_writer` CLI entry point to extract a node's image
  stream to video.

### 1.0.27 — 2026-06-30
- Added the JOYSTICK and SERIAL_TOUCH_SCREEN nodes (a serial-connected
  analog joystick, and a raw serial touch controller source).
- `wait_for` / `wait_for_hold` / `wait_for_dual_hold` now accept `None` for
  their timeout/blink-duration arguments to wait indefinitely; the
  `stimulator` context manager is currently disabled (a no-op) -- tasks
  delivering stimulation should drive `do_stimulation()` directly.

### 1.0.26 — 2026-06-29
- **Breaking (plugins):** the plugin C API no longer uses raw C strings --
  every `char*` parameter/return value is now a `ThalamusCharSpan`. Existing
  plugins need to be ported.
- TOUCH_SCREEN gained a `Null Threshold` parameter to detect "no touch"
  readings.
- DISTORTION gained a `Framerate` parameter to throttle its output frame
  rate.
- Fixed a crash when cancelling chessboard-based calibration/board
  generation.

### 1.0.24 — 2026-06-24
- Loading a task cluster that references an unregistered task type now shows
  an "Unknown Task" dialog instead of crashing the control window.

### 1.0.22 — 2026-06-16
- Eye calibration: added target presets (8 Targets / 4 Targets On Axes / 4
  Targets On Corners) and a Recenter button; the Projective model fit no
  longer includes an offset/bias term (the raw origin now always maps
  exactly to the screen origin).
- An unhandled exception inside a task's `run` now clears the run queue and
  shows a "Task Error" dialog with the traceback, instead of propagating
  silently.

### 1.0.20 — 2026-06-12
- Eye calibration: added a grid overlay showing how the fitted model warps
  eye coordinates across the field, a bolder highlight for the selected
  saccade target, and a more compact operator-window layout with an
  "Unfocused" reminder overlay.

### 1.0.19 — 2026-06-11
- Eye calibration: nudged/added Angular Scaling notches now survive a
  re-Fit instead of being overwritten; Fixation Radius, Saccade Radius, and
  Reward (ms) are now persisted with the configuration; added a
  diagonal-lines/circle reticle to the operator view.  Fixed extrapolation
  beyond a pin's outermost notch (both in the calibrator and at task
  runtime).
- Fixed "Save As" proceeding with an empty file name instead of aborting
  when the user cancelled the file dialog.

### 1.0.18 — 2026-06-10
- STORAGE and STORAGE2 now default to compressing video (H264/MPEG-4) for
  newly created nodes; uncheck "Compress Video" if you need raw, lossless
  frames.
- Eye calibration: added an Eye Opacity slider for the gaze-trace overlay.
- Python packaging now depends on `pandas` and `pyarrow` (required by
  `thalamus.dataframe`).

### 1.0.17 — 2026-06-09
- Eye calibration: implemented notches for the Angular Scaling model,
  letting gaze scale vary with eccentricity along a direction, not just by
  direction.
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
