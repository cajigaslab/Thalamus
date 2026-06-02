# Changelog

This changelog summarizes notable, user-facing changes per release.  It is curated
from the project's commit history; the canonical list of releases and downloadable
wheels lives on the [Releases page](https://github.com/cajigaslab/Thalamus/releases).

Thalamus uses `MAJOR.MINOR.PATCH` versions.  Releases are produced automatically, so
some patch versions contain only build/CI or internal changes and are omitted below.

## 1.0.x

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
