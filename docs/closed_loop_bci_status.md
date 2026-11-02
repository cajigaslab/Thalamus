# Closed-Loop Online BCI — Status & Index (as of 2026-11-02)

The single entry point for the closed-loop BCI work on the Eevee/AlexRig. Goal:
**live INTAN neural data → features → decoded cursor command → cursor on the
240 Hz subject display**, at the lowest latency we can get, safe to run with a
trained animal. This page is the summary: what exists, what's been measured,
and what's still open. The detailed docs it links to have the full picture.

| Doc | Covers |
|---|---|
| `rust_bci_patch.md` | The low-latency Rust task executor that renders the cursor (M1–M4, multiple task machines) |
| `closed_loop_decoder.md` | `neural_decoder.py`: injection contract, pipeline, CLI, latency budget, ggml plan |
| `joystick_control_modes.md` | `direct` / `cumulative` / `blend` math, and the case for velocity control under a decoder |
| `phase0_safety_and_intan_baseline.md` | Phase 0 safety layer, INTAN soak-test tool, 5ch and 128ch streaming results |

## Architecture in one picture

```
Intan RHX (192.168.1.241)
   │ TCP waveform stream, 128-frame blocks
   ▼
INTAN node (C++, src/intan_node.cpp) ── 128 ch, A-000..A-127
   │ analog RPC stream
   ▼
neural_decoder.py   filter → features → velocity model → (integrate) 
   │ ONE long-lived inject_analog stream
   ▼
"Decoder" ANALOG node (X, Y)          ◀── same shape as the hardware "Joystick" node
   │ analog RPC stream
   ▼
Rust executor (rust/joystick_task)    joystick_node="Decoder", 240 Hz,
   │                                  control_mode direct|cumulative|blend,
   │                                  Phase 0 input-safety gate
   ▼
subject display (override-redirect fullscreen, fifo, paced) ──▶ photodiode (NIDAQ Dev1/ai1)
```

The decoder writes X/Y into a node the task already knows how to read. Closing
the loop needed **no Rust or C++ changes**, and later swapping the model
(Python → ggml) changes nothing downstream either.

## What's done

### Rendering / task side
- **Rust executor in production** for `joystick_intro` (M1–M4). It also runs
  `simple_touch_rust` behind a `TrialMachine` trait seam (2026-11-02).
- **Control modes:** `direct`, `cumulative`, and `blend`. `blend` is a
  leaky-integrator ramp from direct to cumulative, with tau = blend/(1−blend) s.
  It gives a graded training path toward velocity control, the paradigm a
  decoder should drive.
- **Joystick hardware contract fixed** (2026-07-20). The Pro Micro firmware
  re-biases rest to 512, eevee.json centers are a fixed 512/512, and the
  deadzone lives only in the node. This removed the rest-offset/double-deadzone
  problem, so the joystick→Decoder swap compares like with like.
- **Executor robustness:** one shared `ThalamusConn` instead of a connection per
  trial (the per-trial version leaked 429 threads), graceful SIGINT/SIGTERM, and
  stdout logging after the 2026-08-26 wedge hang.

### Decoder (`neural_decoder.py`, 2026-07-14)
- End-to-end closed loop verified live. Loopback circle ✅; cursor responds to
  live channels ✅.
- ~148 Hz decode (one decode per INTAN packet). Compute p50 ~0.15–0.3 ms.
  recv→inject p50 ~0.3 ms.
- Reconnect-resilient (`run_forever`). Opt-in via `./run_decoder.sh`; it is not
  orchestrated.
- Model stage is still a **placeholder**: per-channel RMS → fixed linear W, with
  opponent channel pairs.

### Latency — measured with the photodiode
| Measurement | Result | Source |
|---|---|---|
| Render floor, command→photon (production override-redirect path) | **p50 10.4 / p90 11.9 / max 12.5 ms** | `spike` tool, 2026-07-14 |
| Same, but WM-composited fullscreen | p50 23.7 ms (Mutter adds ~13 ms) | `spike` tool |
| Behavioral display latency, real sessions (py-proc `display_behave`) | **~27–29 ms mean**, σ ~6 ms, flat across 3 days / 1709 trials | 2026-08-13 |
| Estimated neural event → photon | ~18–20 ms p50 (+0–6 ms INTAN block jitter) | budget: block + decode + transport + render floor |

The ~11 ms spike number and the ~27 ms py-proc number come from the same rig
and photodiode but have **not been reconciled** (see Open items). Part of the
gap: the diode square scans out ~4 ms after top-of-frame, plus LCD response
time. Also, `proc_display.py`'s "median display latency" is a heuristic seed,
not a measurement. Trust `pyCheck/display_behave.py`.

### Phase 0 safety layer (2026-09-08, first slice)
- Non-finite (NaN/Inf) samples are dropped at the source (`control::pump_input`).
- `Trial::step` holds the last good input when a sample is non-finite or stale
  (no fresh sample for >250 ms).
- If bad input persists ≥500 ms during an active trial, the trial fails with
  `"invalid_input"`.

### INTAN streaming (2026-09-08 → 09-22)
- `intan_soak_test.py`: read-only stream soak tool (gaps, drift, NaN, shape)
  with a `--duration` timeout fix.
- INTAN node scaled **5 → 128 channels** (A-000..A-127).
- The INTAN node's receive buffer now scales with channel count. The old fixed
  16 KB buffer held only about half of a 128-channel packet.
- 30-minute soaks:

| Run | Rate | Packets | Sample shortfall | NaN / bad shape |
|---|---|---|---|---|
| 5 ch, 2026-09-08 | 20 kHz | 280,137 | 0.39 % (−7.1 s) | 0 / 0 |
| 128 ch, 2026-09-08 | 20 kHz | 277,503 | **1.33 %** (−23.9 s) | 0 / 0 |
| 128 ch, 2026-09-22 | 30 kHz | 421,870 | **≈0 %** (−4.5 ms) | 0 / 0 |

Delivery comes in ~64 ms bursts of ~10 packets, from RHX's own flush cadence.
The ~10 % "anomalous packets" figure in every run is that burst structure, not
data loss. See `phase0_safety_and_intan_baseline.md` Part 3 for how to read
these runs and what they don't prove.

## Open items (roughly in dependency order)

**Safety (Phase 0 gate, required before a decoder runs with an animal):**
1. Operator kill-switch: instantly force `joystick_node` back to `"Joystick"`
   without a restart.
2. Automated fault-injection bench: NaN / frozen / killed decoder / out-of-range
   values, 100 % fail-safe.
3. Decide whether `step_free_play` needs the same sustained-fault abort.

**INTAN streaming (Phase 2 gate: ~244 channels, zero dropped packets):**
4. Run a 128 ch soak **at 20 kHz on the current build**. This separates the
   effect of the buffer fix from the 30 kHz switch (see Part 3).
5. Explain the Sep 18 "neurobuild" 128 ch 20 kHz runs that lost ~75 %.
6. Scale to ~244 channels and rerun the soak.
7. Optional: look for an RHX flush-interval setting, to cut the ~64 ms
   batching.

**Latency:**
8. "Closed-loop spike": drive the photodiode square from injected Decoder X to
   measure true decoder→photon latency. All clocks are CLOCK_MONOTONIC, so the
   timestamps compare directly. This also reconciles 11 ms vs 27 ms.
9. Re-derive the budget at 30 kHz. The INTAN block is 128/30000 = **4.27 ms**
   (previously 6.4 ms at 20 kHz), so the decode budget tightens to match.

**Decoder:**
10. Run the velocity paradigm live: task in `cumulative`/`blend` with
    `--emit-mode velocity`. This milestone is still unchecked.
11. Real features (band power / threshold-crossing rates across 128 ch) and a
    fitted decoder (velocity Kalman / ReFIT-style recalibration).
12. Shared control: blend decoded and real joystick output for assisted
    training.
13. When the model grows: move stages 2–4 to a ggml-backed Rust/C++ decoder.
    The injection contract stays the same.

## Operating gotchas (the ones that have actually bitten)
- **Edit `eevee.json` only while the core + UI are down.** The app rewrites it
  from memory on shutdown.
- **After a Rust edit:** `cargo build --release`, then restart. eevee.json runs
  the release binary.
- **Reuse one `inject_analog` stream.** A churned stream can spin
  `grpc_impl.cpp`'s inject loop.
- **Running the `spike` tool:** the core must be up and the executor stopped,
  and always pass `--override-redirect`.
- Never reorder the eevee.json `nodes` list while the stack is live.
