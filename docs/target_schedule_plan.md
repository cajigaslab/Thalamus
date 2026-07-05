# Implementation guide: structured target schedules for joystick_intro

> Self-contained, execution-ready plan. Meant to be run later on the Linux rig with the full
> system (Thalamus core, Rust executor, cameras) available so it can be tested end-to-end.
> Repo-relative paths throughout. Nothing here depends on any planning conversation.

## 1. Context / goal

`joystick_intro` presents targets by **uniform random choice** over the enabled target list.
It's a great intro-to-joystick task, but has hit a ceiling for *teaching cursor control*: we
want to move from random targets to **structured** presentation while keeping everything else
(Rust low-latency executor, reward, sound, `behav_result` logging, animations, layout editor).

The "how is the next target chosen" decision lives in exactly one seam
(`place_target()` in `state.rs`), so this is a **targeted expansion of the existing task**, not a
new task. We add three schedule modes and an interleave ratio, author them in the existing
layout editor, and tag each trial's schedule phase in the log.

Confirmed decisions:
- **Modes:** `center_out`, `sequence` (fixed order), `random` (current), with an
  `interleave_random_ratio` mixing random draws into a structured stream.
- **Authoring:** extend the Target Layout Editor.
- **Runtime:** **Rust only.** The pure-Python `run()` does NOT honor schedules; the shared editor
  UI still writes the config keys. Intentional, documented divergence.
- **Logging:** tag each trial with its schedule phase in `behav_result`.

## 2. Architecture recap (so this doc stands alone)

- **`thalamus/task_controller/joystick_intro.py`** — the canonical Python module. Holds BOTH the
  Qt operator config UI (`create_widget`) and a pure-Python `run()` trial loop. Key seams:
  layout editor dialog opens at `open_layout_editor` (~L850); `save_layout` (~L1595) writes
  `draft_targets` → `task_config["targets"]`; named presets live at
  `task_config["target_layout_presets"]` (`save_preset`/`load_preset`, ~L1628-1665);
  pure-Python `place_target` (~L2512) does `random.choice`.
- **`thalamus/task_controller/joystick_intro_rust.py`** — thin gRPC delegate. Reuses
  `joystick_intro.create_widget` verbatim and forwards to the Rust executor. Sends the whole
  config as `json.dumps(task_config.unwrap())`, so **any new config key rides across for free**.
- **`rust/joystick_task/`** — the production executor (bin `joystick_intro_executor`). Owns the
  trial state machine, target selection, hit-detection, rendering, reward, logging. Modules:
  - `src/config.rs` — `TaskConfig`, deserialized from `config_json`. Unknown keys are ignored
    (see test `unknown_keys_are_ignored`), so old configs keep parsing.
  - `src/state.rs` — the trial state machine. Selection seam: `place_target()` (~L562),
    `ensure_next_target_preview()`/`consume_next_target()` (~L588-599); trial start in
    `start_trial()` (~L865). `TargetSelection` struct ~L124. Seeded xorshift `Rng` ~L139.
  - `src/events.rs` — `behav_result` model with strict key-order/byte-compat rules + tests.
- **`proto/rust_task.proto`** — Python↔Rust contract. **No proto change needed** (config travels
  as a JSON string; result travels as `behav_result_json`).

## 3. Data model — one new config key: `target_schedule`

A single dict alongside `targets` in `task_config`. Targets referenced **by name** (stable,
human-readable). Missing/disabled names fall back to a random draw.

```json
"target_schedule": {
  "mode": "random",                  // "random" | "sequence" | "center_out"
  "order": ["U1", "R1", "D1", "L1"], // mode=sequence: ordered target names; cycles/loops
  "center": "C",                     // mode=center_out: name of the center target
  "peripherals": [],                 // mode=center_out: ring names; empty => all non-center enabled
  "peripheral_order": "sequential",  // "sequential" | "random"
  "interleave_random_ratio": 0.0     // 0..1 prob. a trial is a random draw instead of the scheduled one
}
```

Semantics:
- **random** (default): today's behavior, unchanged.
- **sequence**: cycle `order`, looping forever.
- **center_out**: alternate `center` → peripheral → `center` → next peripheral …; peripheral
  chosen `sequential` (cycle the ring) or `random` (uniform over ring).
- **interleave** (applies on top of sequence/center_out): with prob. `interleave_random_ratio`,
  the trial is replaced by a uniform-random enabled target AND the structured cursor **does NOT
  advance** — the pattern resumes where it left off (this is the "interwoven" behavior). At
  `mode=random` the ratio is irrelevant.

Backward compatibility: absent key ⇒ `mode:"random"` ⇒ identical to today.

## 4. Rust changes — `rust/joystick_task/`

### 4.1 `src/config.rs`

Add a typed schedule struct with serde defaults (follow the existing `lenient::*` deserializer
pattern used elsewhere in this file for Qt-typed floats/bools), and a field on `TaskConfig`.

```rust
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(default)]
pub struct TargetSchedule {
    pub mode: String,                 // "random" | "sequence" | "center_out"
    pub order: Vec<String>,
    pub center: String,
    pub peripherals: Vec<String>,
    pub peripheral_order: String,     // "sequential" | "random"
    #[serde(deserialize_with = "lenient::f64")]
    pub interleave_random_ratio: f64,
}

impl Default for TargetSchedule {
    fn default() -> Self {
        Self {
            mode: "random".into(),
            order: Vec::new(),
            center: String::new(),
            peripherals: Vec::new(),
            peripheral_order: "sequential".into(),
            interleave_random_ratio: 0.0,
        }
    }
}
```

On `TaskConfig` (near the `targets` field, ~L214): add
```rust
    #[serde(default)]
    pub target_schedule: TargetSchedule,
```
and in `Default for TaskConfig` add `target_schedule: TargetSchedule::default(),`.

### 4.2 `src/state.rs` — the core change

**(a) Tag the selection.** Add a phase to `TargetSelection` (~L124):
```rust
    pub schedule_phase: String, // "random" | "sequence" | "center" | "peripheral"
```
Set it in `parse_target`'s constructed value (default `"random"`) and overwrite in the scheduler.
Every place that currently builds a `TargetSelection` literal (the fallback in `place_target`,
~L574) must set this field — grep for `TargetSelection {` to find them all.

**(b) Scheduler state on `Trial`.** Add a field, e.g.:
```rust
struct SchedulerState {
    seq_pos: usize,
    center_out_expect_center: bool, // true => next structured pick is the center
    peripheral_pos: usize,
}
```
Initialize in the `Trial` constructor: `center_out_expect_center: true`, others `0`.

**(c) Rewrite `place_target()` (~L562).** New logic (keep the empty-list fallback):
1. Build `enabled` exactly as today.
2. If `enabled.is_empty()` → return the existing fallback target (with `schedule_phase:"random"`).
3. Let `sched = &self.cfg.target_schedule`. If `sched.mode == "random"` → random pick (as today),
   `schedule_phase = "random"`.
4. Else roll interleave: `if sched.interleave_random_ratio > 0.0 && self.rng.next_f64() <
   sched.interleave_random_ratio` → random pick tagged `"random"`, **return without advancing**
   the scheduler. (Confirm/add an `rng.next_f64()` in `[0,1)`; the xorshift `Rng` at ~L139 already
   has `choice`; add a float helper if missing.)
5. Else compute the next structured **name**:
   - `sequence`: `name = order[seq_pos % order.len()]`; then `seq_pos += 1`. Tag `"sequence"`.
   - `center_out`: if `expect_center` → `name = center`, tag `"center"`, flip `expect_center=false`.
     Else pick a peripheral (ring = `peripherals` if non-empty else all enabled names except
     `center`); `sequential` uses `peripheral_pos` then increments, `random` uses `rng.choice`;
     tag `"peripheral"`; flip `expect_center=true`.
6. Resolve `name` → the matching **enabled** `TargetSelection` (linear scan of `enabled` by the
   target's `name` field). If not found/disabled → fall back to a random pick tagged `"random"`
   (do not get stuck; the cursor already advanced, which is fine — a missing name just yields a
   random trial and the pattern continues next slot).

Note: `parse_target` currently does not read `name`. Add name extraction so the scheduler can match
(`obj.get("name").and_then(Value::as_str)`), stored on `TargetSelection` or matched inline.

**(d) Do NOT restructure the preview pipeline.** `ensure_next_target_preview`/`consume_next_target`
(~L588-599) already call `place_target()` exactly once per target that will be shown, so a
per-call cursor advance stays consistent and the one-deep preview shows the genuine next target.
**Verify** (grep) that nothing else calls `place_target`/`ensure_next_target_preview`
speculatively — if the operator "next target" HUD calls it, that's fine (it IS the next target),
but any extra call would double-advance the scheduler.

**(e) Propagate the tag in `start_trial()` (~L865).** After `consume_next_target()`:
- set `a.schedule_phase = Some(target.schedule_phase.clone());` on the attempt,
- add `.with("schedule_phase", target.schedule_phase.clone())` to the `target_on` event (~L893).

### 4.3 `src/events.rs` — trial tagging

Add to `Attempt` (struct ~L101) **immediately after `failure_reason`** (before the `free_play`
and `end` flatten fields):
```rust
    pub schedule_phase: Option<String>,
```
Init `None` in `Attempt::new` (~L130). Placing it after `failure_reason` preserves the existing
`starts_with(... "failure_reason":null)` assertion in
`attempt_initial_keys_match_reset_attempt_tracking` (~L312), so **no test edit is required** there.

**Downstream caveat:** this adds one additive key (`schedule_phase`) to `behav_result`. Confirm the
`record_reader` / analysis path tolerates an extra key (it should — additive). Grep the analysis
code for a strict schema/whitelist before running a real session.

### 4.4 Rust tests (`state.rs` `#[cfg(test)]`, `config.rs`)

Add unit tests:
- `config`: `target_schedule` default is `mode=="random"`; a populated JSON parses.
- `sequence`: repeated `place_target` yields `order` cycling and looping; all tagged `"sequence"`.
- `center_out`: strictly alternates `center`/`peripheral`; `sequential` cycles the ring;
  `random` stays within the ring.
- `interleave`: ratio `0.0` ⇒ never `"random"` tag and cursor advances every call; ratio `1.0` ⇒
  always `"random"` and the structured cursor never advances (seed the `Rng` deterministically).
- `missing name` ⇒ falls back to `"random"` without panicking.

## 5. Python authoring — `thalamus/task_controller/joystick_intro.py`

All edits are inside `create_widget` (the shared config/editor builder). Back the UI with a
`draft_schedule` dict (a `nonlocal`/closure var next to `draft_targets`, ~L872), seeded from
`normalize` of `task_config.get("target_schedule", {})` with the section-3 defaults.

**Add a "Schedule" `QGroupBox`** to the editor side panel near the Presets row (~L924):
- **Mode** `QComboBox`: Random / Fixed Sequence / Center-Out → sets `draft_schedule["mode"]` to
  `random`/`sequence`/`center_out`. Its `currentIndexChanged` shows/hides the controls below.
- **Center-Out controls:** a "Center target" `QComboBox` populated from current draft target
  names (repopulate whenever the target list changes — hook the same refresh that rebuilds the
  target list); a "Peripheral order" `QComboBox` (Sequential / Random). Peripherals left empty ⇒
  all non-center enabled.
- **Fixed Sequence controls (v1, lean):** order = enabled targets in **list order**. Add
  **Move Up / Move Down** buttons operating on `draft_targets` + the existing `target_list`
  (`QListWidget` ~L902) so the operator can reorder. (A dedicated subset/order widget can come
  later; v1 keeps it simple.)
- **Interleave random %** `QDoubleSpinBox` (range 0.0–1.0, step 0.05) → `interleave_random_ratio`.

**Persist on save.** In `save_layout()` (~L1595), after writing `targets`, also write:
```python
task_config["target_schedule"] = build_draft_schedule()  # reads the controls into the section-3 dict
```
where for `sequence` you set `order = [t["name"] for t in draft_targets if t.get("enabled", True)]`.

**Presets carry the schedule too.** Update `save_preset`/`load_preset` (~L1628-1665):
- New preset shape: `{"targets": [...normalized...], "schedule": {...draft_schedule...}}`.
- `load_preset` must be **backward-compatible**: if the stored preset is a bare `list`, treat it
  as targets-only and set `draft_schedule = {"mode": "random", ...defaults}`. If it's a dict,
  read `["targets"]` and `["schedule"]`.

**Do NOT change** the pure-Python `place_target` (~L2512). Add a one-line comment there noting the
Rust executor owns scheduling and the Python path intentionally stays random-only.

## 6. Docs

Update `docs/joystick_intro_task_guide.md`: document the `target_schedule` schema (section 3), the
new `schedule_phase` key in `behav_result`, and the note that scheduling is honored by the **Rust
executor only** (pure-Python `run()` remains random).

## 7. Verification (on the Linux rig)

Build + unit tests:
```bash
cd rust/joystick_task
cargo build
cargo test          # new scheduler tests + existing parity tests must pass
```

End-to-end (full system running — Thalamus core, orchestrated Rust executor, cameras):
1. Launch the task controller; open the **Target Layout Editor**.
2. Use the annulus generator to place a center + ring (the ring IS your center-out target set).
3. Set **Mode = Center-Out**, **Peripheral order = Sequential**, **Interleave random % ≈ 0.30**,
   pick the center target, **Save**.
4. Run trials. On the subject display confirm: targets alternate center ↔ periphery, with ~30%
   random insertions, and the structured pattern **resumes** after each random insertion.
5. Inspect a dumped `behav_result` (via `trial_summ` / record_reader): every attempt carries the
   correct `schedule_phase` (`center` / `peripheral` / `sequence` / `random`).
6. Set **Mode = Fixed Sequence**, reorder with Move Up/Down, run; confirm the order cycles.
7. Regression: set **Mode = Random** (or load an old config with no `target_schedule`); confirm
   behavior is identical to before this change.

## 8. Rollout / risk notes

- **No proto change**, **no schema break**: absent `target_schedule` ⇒ old behavior; extra
  `schedule_phase` key is additive.
- Primary risk is the **`record_reader` schema** rejecting the new key — check before a real
  session (§4.3).
- Keep the change in the Rust executor + shared editor UI only; the pure-Python `run()` divergence
  is intended and documented.
- Suggested commit slicing: (1) config + scheduler + tests in Rust; (2) events tagging; (3) editor
  UI + preset compat; (4) docs.
