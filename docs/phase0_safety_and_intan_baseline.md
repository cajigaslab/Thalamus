# Phase 0 Safety Layer + INTAN Streaming Baseline (2026-09-08)

Session log for the first concrete work on the closed-loop BCI roadmap (see
`closed_loop_decoder.md` for the decoder pipeline this feeds into). Two independent
work streams:

1. A read-only INTAN soak-test tool, and the 5-channel streaming baseline it
   established -- the reference point for the roadmap's channel-scaling gate
   ("zero dropped packets" at ~244 channels).
2. The first slice of the safety/fail-safe layer in the Rust joystick task --
   the hard gate the roadmap requires before a decoder runs anywhere near a live
   animal session.

## Part 1 -- `intan_soak_test.py`: INTAN streaming baseline

### What it is

A standalone, read-only script (repo root, modelled on `neural_decoder.py`'s
subscribe pattern but with no filter/model/injection stage) that subscribes to
the INTAN node's analog stream and measures stability: inter-packet gaps,
cumulative sample-time drift, per-channel shape consistency, and non-finite
values. No decoder, no injection -- it measures the INTAN-to-client leg only.

```
python intan_soak_test.py                          # 5ch default, runs until Ctrl-C
python intan_soak_test.py --duration 1800           # 30-minute soak
python intan_soak_test.py --channels A-001,...,A-244  # future full-scale run
```

Per-packet detail goes to a timestamped CSV (`intan_soak_*.csv`, gitignored --
these are run data, not source); a rolling summary prints every `--log-every`
packets, and a final report prints duration, packet count, gap percentiles,
cumulative drift, anomaly/NaN/bad-shape counts, and throughput.

### Findings

**Short soaks (~20s) are misleading -- don't trust them.** Early runs showed
what looked like a severe ~13-15% sustained data shortfall, growing
continuously with no sign of leveling off. Investigated and ruled out, in
order:

- **Core CPU contention from concurrent nodes** (4 camera `image_viewer`
  processes + the joystick task were live; the core process was at 173% CPU).
  Ruled out: rerunning after stopping the cameras (core CPU dropped to ~1%)
  produced an almost identical drift pattern.
- **Wrong assumption about the configured sample rate.** Checked
  `src/intan_node.cpp:395-436` -- the sample rate is genuinely queried live
  from the RHX device via `get sampleratehertz`, not hardcoded, so the
  ~156.25 Hz / 6.4ms expectation (128 samples @ 20kHz) was correct.
- **Network.** Ping to the acquisition machine (192.168.1.241): 1-2ms RTT, 0%
  loss. Not the bottleneck.

**The real behavior, confirmed by a clean 30-minute run** (280,137 packets,
zero NaN, zero malformed packets):

- Data does not arrive smoothly every 6.4ms. It arrives in **regular bursts of
  ~10 packets (1280 samples) every 64.0ms** (std dev 2.5ms across the whole
  run) -- a fixed batching cadence, almost certainly the RHX acquisition
  software's own network flush interval, not a fault in this codebase. The
  earlier "~13-15% anomalies" figure was mostly this batching structure being
  misclassified by a naive `gap > 3x expected interval` heuristic, not real
  loss.
- On top of that structural batching there IS a small, real, persistent
  shortfall: **~0.40% fewer samples than expected per 64ms cycle.** Segmenting
  the 30-minute run into thirds: first third lost 3.47s (of drift), middle
  1.80s, last 1.80s -- i.e. an early transient (likely subscription/connection
  warm-up) that settles into a **stable ~0.3% steady-state loss rate that does
  not grow or accelerate** over a sustained 30-minute run.

### Baseline reference number

**~0.3% steady-state sample shortfall at 5 channels, riding on a rock-solid
64ms/10-packet delivery cycle.** Use this as the point of comparison for the
roadmap's Phase 2 gate ("scale INTAN to ~244 channels, zero dropped packets"):
rerun `intan_soak_test.py` at each channel-count step. If the shortfall
percentage stays flat, it's inherent to RHX's batching and channel-count
independent (fine). If it grows with channel count, something channel-count
dependent (bandwidth, buffer sizing, the core's single `io_context` thread) is
compounding it and needs attention before going further.

**Open, non-blocking lead:** the 64ms cadence is very likely a configurable
buffer/flush-interval setting in the RHX software itself. Worth a look if
lower-latency streaming becomes a priority, but nothing here blocks scaling up
channel count.

## Part 2 -- Phase 0 safety layer (first slice)

The roadmap's Phase 0 requires closing this gap before a decoder runs near a
live animal: today, a hung/crashed decoder or a NaN/Inf sample from any analog
source is held by the Rust task forever, with no alarm, no fallback, no trial
abort.

### What landed

- **`rust/joystick_task/src/input.rs`** -- `JoystickState` now tracks the
  arrival time of its last accepted sample (`age()`) and a dropped-sample
  fault counter (`fault_count()`), alongside the existing `(x, y)`.
- **`rust/joystick_task/src/control.rs`** (`pump_input`, the live trial path)
  -- drops any non-finite `(x, y)` sample before it reaches the trial state
  machine or the cursor cell, instead of forwarding NaN/Inf downstream.
- **`rust/joystick_task/src/state.rs`** (`Trial::step`) -- sanitizes analog
  input at the top of every frame: non-finite *or* stale (no fresh sample in
  250ms) input falls back to the last known-good value rather than touching
  `cursor_x`/`cursor_y`. If that condition persists **>=500ms**
  (`INPUT_FAULT_TRIAL_ABORT_S`) during an active trial, `step_target_trial`
  fails the trial with reason `"invalid_input"`, mirroring the existing
  `"timeout_without_movement"` path.
- **`rust/joystick_task/src/render/mod.rs`** -- computes staleness from
  `job.joystick.age()` each frame and feeds it into `StepInput`.

### Explicitly deferred (not done in this pass)

- **Operator kill-switch**: an instant, code-level way to force
  `joystick_node` back to `"Joystick"` without a stack restart. Today this
  still relies on the existing operator-UI combo box.
- **Formal fault-injection bench suite**: the roadmap's Phase 0 gate is
  "100% fail-safe across NaN / frozen / killed / out-of-range fault
  injection, zero tolerance." What's been run so far is a manual smoke test
  (see below), not that full automated suite.
- Only `step_target_trial` got the sustained-fault trial-abort; `step_free_play`
  was left unchanged (scoping choice -- BCI sessions run the target/center-out
  trial path).

## Status against the roadmap

- **Phase 0** (safety): input sanitization + staleness detection + trial-abort
  landed and smoke-tested; kill-switch and the full bench suite are still
  outstanding before this phase can be marked complete.
- **Phase 2** (scale INTAN): 5-channel baseline established and characterized
  precisely (above); next step is repeating the same soak test at higher
  channel counts once that hardware is wired up.
