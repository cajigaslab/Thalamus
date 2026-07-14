# Closed-Loop Neural Decoder → Joystick Cursor

Design + operating doc for `neural_decoder.py`, the first closed-loop stepping stone
toward a cursor-decoding brain-machine interface. Companion to `rust_bci_patch.md`
(the low-latency Rust task that renders the cursor) and the code under
`rust/joystick_task/`.

## Context / goal

INTAN streams live neural channels (currently `A-001`..`A-005`, fake demonstration
voltages; eventually intracortical / surface cortical electrodes) into the Thalamus
core. The goal is a real closed loop: **streamed neural data → features → an inferred
joystick command → cursor motion** in the existing Rust `joystick_intro` task, with
latency as a first-class concern.

The whole loop is built with **zero changes to the Rust task and zero C++ recompiles**.

## The key idea: the injection contract

The Rust task subscribes to a *configurable* analog node (`joystick_node`) for channels
`["X", "Y"]`. It does not care whether those samples came from a hardware joystick or
from somewhere else. So a decoder that injects `X`/`Y` into an `ANALOG`-type node named
`Decoder` is **indistinguishable to the task from the real joystick**.

```
INTAN node ──analog RPC (stream)──▶  neural_decoder.py
  128-sample blocks, 20 kHz            listener → filter → features → velocity model → integrate?
  (≈6.4 ms fixed input floor)                        │
                                                     ▼ inject_analog (ONE long-lived stream)
                                            "Decoder" ANALOG node  (X, Y)
                                                     │
                                            analog RPC (stream) ──▶ Rust joystick task
                                                                     joystick_node="Decoder"
                                                                     240 Hz, last-sample-wins,
                                                                     direct|cumulative → cursor
```

Consequences:
- **BMI control:** set the task's `joystick_node` to `"Decoder"`.
- **Real joystick:** set it back to `"Joystick"` (unchanged legacy behavior).
- **Shared control / assist (future):** the decoder can subscribe to *both* INTAN and
  `Joystick` and blend `out = α·decode + (1−α)·joystick` before injecting.

## Control law: decode velocity, match the task's mode

The decoder **always computes a velocity** `(vx, vy)`. A final stage matches the task's
`control_mode` so one decode core serves both cursor paradigms (`--emit-mode`):

| `--emit-mode` | Decoder does | Use with task `control_mode` |
|---|---|---|
| `position` (default) | leaky-integrates velocity → emits **position** | `direct` (the currently trained mode) |
| `velocity` | emits **raw velocity**, task integrates | `cumulative` |

Position integrator: `pos = clip(leak·pos + gain·v·dt, −1, 1)` per axis, real elapsed
`dt` (clamped ≤50 ms). `leak < 1` gives natural recentering and prevents drift/runaway
— it mirrors the task's `direct_recenter_when_idle`.

> Do **not** feed a position signal into a `cumulative` task (double integration) or a
> velocity signal into a `direct` task (velocity treated as absolute position). Match
> `--emit-mode` to `control_mode`.

## The decoder pipeline (`neural_decoder.py`, repo root)

Standalone async Python process, modelled on `fiducial.py` / `intan.py`. Each stage is a
deliberately swappable seam:

1. **Listener** — subscribes to the source node's `analog` stream, keeps a per-channel
   rolling window, timestamps each packet (`t_recv`).
2. **Filter (placeholder)** — per-channel `scipy.signal` IIR bandpass with persistent
   state (`zi`) to avoid per-packet edge transients. **Identity passthrough by default**
   (`--band` unset). Seam for real DSP.
3. **Model (placeholder)** — per-channel RMS over the window → feature vector →
   linear readout `W (2×n) + …` → velocity. Default legible 5-channel mapping:
   `A-001 − A-002 → vx`, `A-003 − A-004 → vy` (opponent channels self-center, so a
   steadily-louder channel yields a *sustained* velocity). **This is where a real
   decoder goes** (velocity Kalman filter, population vector, NN). Feature
   normalization / whitening belongs here too — *not* a temporal baseline, which would
   wash constant activity to zero.
4. **Integration** — mode-matched output (see control law above).
5. **Injector** — one long-lived `inject_analog` stream to `Decoder`, reused for the
   process lifetime (`t_inject`).

**Resilience:** `run_forever` wraps connect + run in a reconnect loop (mirrors the Rust
executor's `connect_lazy`). It **blocks on `channel_ready()` until the core first comes
up** (so it can be started before the core) and **reconnects with backoff** when the core
restarts — rebuilding the analog subscription and inject stream and re-deriving `fs`.
Essential for an orchestrated decoder, since the orchestrator does not auto-respawn a
non-critical process that exits.

**Cadence:** one decode per INTAN packet (event-driven, ~6.4 ms, deterministic). Use
`--decimate N` to decode every Nth packet if compute grows (the dropped rate is logged,
never silently capped).

### CLI
```
python -u neural_decoder.py [options]
  --thalamus localhost:50050     core gRPC address
  --source   INTAN               analog node to decode from
  --target   Decoder             ANALOG node to inject X/Y into
  --channels A-001,A-002,A-003,A-004,A-005
  --window-ms 200                feature window
  --emit-mode position|velocity  match the task's control_mode
  --gain 1.0                     velocity gain
  --leak 0.98                    position-integrator leak per tick
  --band "low,high"              bandpass Hz (empty = identity filter)
  --decimate 1                   decode every Nth packet
  --log-every 200                latency report interval (ticks)
  --loopback                     ignore neural data, emit a slow circle (plumbing test)
```

## Configuration in `eevee.json`

1. **`Decoder` node** — an `ANALOG` node in the `nodes` list (mirrors `Reward` /
   `Fiducial`). Injection targets an ordinary `ANALOG` node; `inject_analog` sets its
   data and fires `ready`, so subscribers see injected samples as if hardware-produced.
2. **Orchestration process** — an entry in `Orchestration.Processes`:
   `python -u /…/neural_decoder.py` (use `-u` so stdout streams unbuffered into the
   orchestrator log; non-critical so a decoder crash can't take down the rig).
3. **Point the task at it** — set a joystick task's `joystick_node` to `"Decoder"`
   (operator UI combo box, or the task config). Keep `control_mode: "direct"` for the
   default `--emit-mode position`.

### ⚠️ eevee.json persistence gotcha (read this)

The running app **rewrites `eevee.json` from its in-memory observable state on
shutdown**. Two consequences:

- A **text edit made while the stack is running** can be silently reverted on the next
  shutdown. Edit `eevee.json` as text **only while the core + UI are DOWN** (nothing on
  ports 50050 / 50051), then start — it loads the edit. Otherwise edit through the
  operator UI (e.g. the **Orchestration dialog**), which updates the in-memory state
  that gets persisted.
- Editing the config file does **not** relaunch an already-running orchestration
  process; the changed command only takes effect on the next stack start.

## How it runs — verified numbers (2026-07-14)

- **INTAN:** `A-001`..`A-005`, 128 samples/packet, 20 kHz (interval 50000 ns) →
  **≈6.4 ms** packetization floor (set by acquisition, not tunable in software).
- **Loopback:** client emit ~96 Hz, decode compute p50 ~4 µs.
- **Neural (position mode, 200 ms window, identity filter):** ~**148 Hz** decode rate
  (one per packet), decode compute **p50 ~280 µs / max ~1.6 ms**, `recv→inject`
  p50 ~300 µs. With stationary fake voltages the cursor settles at a fixed offset
  (constant RMS differences → constant velocity → integrator settles); it centers when
  channel amplitudes match and displaces when they differ.

**Latency budget (approx):** INTAN block (~6.4 ms) + decode (<1 ms) + gRPC/io_context
hops (few ms) + task command-to-photon (~10 ms p50 @240 Hz) ≈ **18–20 ms** end-to-end,
plus up to ~6 ms INTAN quantization jitter.

**Measured render floor (2026-07-14, `spike` tool, photodiode on Node 5/Photodiode):**
- override-redirect fifo, `--pace-margin-ms 1.5` (production path): command-to-photon
  **p50 10.4 / p90 11.9 / max 12.5 ms**, 240 fps.
- WM-borderless-fullscreen (composited), same settings: **p50 23.7 ms** — Mutter adds
  ~13 ms. Lesson: the production override-redirect surface is essential; do not regress
  to a WM-managed fullscreen window.
- Command: `joystick_intro_executor spike --monitor 1 --seconds 30 --present-mode fifo
  --pace-margin-ms 1.5 --override-redirect`. Requires the core up (photodiode via NIDAQ)
  and the executor stopped (frees the subject display). This measures the render floor
  only, not the decoder path — see below.

### Where the latency comes from (and how to cut it)

End-to-end **neural event → photon ≈ 18–20 ms p50** on this rig, broken down by origin:

| Stage | p50 | Origin | Measured? | How to reduce |
|---|---|---|---|---|
| INTAN block | 6.4 ms (+0–6.4 ms jitter) | Intan RHX emits in 128-sample frames at 20 kHz; a sample can't be processed until its block completes (`intan_node.cpp` waveform_loop, 128 frames/emit) | yes (fixed) | smaller acquisition block / higher rate; largely fixed by the hardware |
| Decoder compute | ~0.15–0.3 ms | Python filter + RMS + linear readout per packet (`neural_decoder.py`) | yes (decoder log) | already negligible; not the bottleneck |
| Transport inject→on-screen-command | ~1–3 ms (est.) | gRPC decoder→core, core's single `io_context` fires `ready`, task analog stream delivers, pump sets the lock-free cell | **estimate** | keep the node graph lean; measure precisely with the closed-loop spike |
| Render floor command→photon | **10.4 ms** | swapchain queue + vsync + scanout on the 240 Hz display; the FIFO pipeline holds ~1 frame, then panel scanout | yes (photodiode spike) | override-redirect (saves ~13 ms vs composited); mailbox/immediate present mode trades tearing for latency |

Dominated by the **INTAN block (6.4 ms, fixed)** and the **render floor (10.4 ms)**. The Python
decoder itself adds almost nothing (~0.3 ms) — GIL/scheduling is not the bottleneck at this scale.
The biggest single lever already pulled is override-redirect fullscreen: composited (WM-managed)
fullscreen measured **23.7 ms** vs **10.4 ms** override-redirect — Mutter adds ~13 ms, and the
`_NET_WM_BYPASS_COMPOSITOR` hint alone is not enough on this Mutter.

**Watch-outs:** the core's single `io_context` thread serializes `inject_analog` and all
node processing — keep the graph lean and do DSP in the decoder, not in chained C++
nodes. Reuse exactly one inject stream (never per-tick); a churned/half-closed stream can
spin the server (`grpc_impl.cpp` inject loop). See the `joystick-executor-connection-reuse`
finding.

### Enabling / disabling the decoder

The decoder is **opt-in** — it is NOT in `Orchestration.Processes`, so normal behavior runs do
not start it. To run a closed-loop session, launch it manually (it reconnects on core restart and
waits if started before the core, so it can be launched any time the stack is up):

```
./run_decoder.sh                 # defaults: position mode, INTAN A-001..A-005 -> Decoder
./run_decoder.sh --emit-mode velocity --gain 2.0     # pass-through extra flags
```

Then point a joystick task's `joystick_node` at `"Decoder"` (operator UI). Stop it with Ctrl-C.
Edit `Orchestration.Processes` to re-add it only if you want it always-on again — and remember the
eevee.json persistence gotcha (edit while the stack is down).

## Verification milestones

1. **Loopback** (`--loopback`) — cursor traces a slow circle. Proves node creation,
   injection, task subscription, latency harness. ✅
2. **Neural response** — cursor responds to the live channels; verify per-channel
   influence matches `W`. ✅
3. **Velocity paradigm** — task in `cumulative` mode + `--emit-mode velocity`; confirm
   the task integrates the decoded velocity.
4. **Latency characterization** — decoder `recv→inject` log + the Rust task's photodiode
   `spike` tool for true command-to-photon.

## Roadmap

- Real feature extraction (band power / spike rates) + a fitted decoder (velocity Kalman
  / PVA / NN) in the model seam.
- Recalibration / error-attenuation (ReFIT-style): move integration fully into the
  decoder (`--emit-mode position`) so it owns cursor state, bounds, and assist.
- Shared control: blend decode + real joystick for assisted training.
- If Python scheduling jitter becomes the bottleneck, port the decoder to Rust (mirrors
  how the joystick task itself was ported); the injection contract stays identical.
