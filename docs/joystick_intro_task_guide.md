# Joystick Intro Task Guide

This guide explains what `joystick_intro.py` is doing in plain language and describes the information it records during a run.

## What This Task Is For

The joystick intro task teaches the subject that moving the joystick controls a visible cursor on the screen.

In normal task mode:
- The task waits between trials.
- If center-gated trial starts are enabled, the cursor must first return near the task-region center before the intertrial timer begins.
- A target appears.
- The subject moves the cursor into the target.
- The cursor must stay inside the target for a required hold time.
- If the hold is completed, the trial succeeds and reward is delivered.
- If the time limit expires first, the trial fails.

In free-play mode:
- No target-based trial logic is used.
- The subject can move the cursor freely.
- Free-play can optionally reward joystick exploration without any target.
- The task ends when the configured key is released.

If `fail_on_touch_input` is enabled, touch-screen input produces a failed outcome and plays the failure sound in both normal target-guided mode and cursor-only free-play mode.

## Operator View Overlay

When this task is shown in Operator View, it now draws a white text overlay in the top-right corner of the operator display only.

The overlay shows:
- current target name
- current target size as task-region radius percent
- current reward channel
- next item in the task queue

This overlay is not shown on the subject display.

## Target Authoring Workflow

Targets are still stored as rows in `task_config["targets"]`, and the table-based editing workflow now includes both static and active display style.

Each target has two visual states:
- static style: `target_color` plus `target_opacity`, used while the cursor is outside the target
- active style: `target_active_color` plus `target_active_opacity`, used immediately when the cursor enters the target

The active style switch is deterministic visual feedback and does not depend on the animation settings. The animation settings still control optional effects such as the hold progress ring, success pop, and success particle burst.

The hold progress ring is drawn more prominently when enabled, using the active target color plus contrast outlines so hold progress remains visible across different target colors. `show_success_particles` enables a short radial burst at successful target completion. These effects still respect the animation master switch and the target-animation switch.

The Target Layout Editor now supports two complementary ways of building layouts:

- manual editing and dragging of individual targets
- pattern-based generation for faster bulk placement

The generator is meant to accelerate initial layout creation while preserving the current utility of the editor as a refinement tool.

### Generator modes

- `Annulus / Rings`
  - creates concentric rings of targets around the task-region center
  - useful for direct joystick control because it naturally keeps the center area clear
- `Rectangular Grid`
  - creates evenly spaced rows and columns across the task region
  - supports a circular center exclusion zone so idle direct-control cursor position does not accidentally overlap a target
- `Hexagonal Packing`
  - creates staggered rows for denser and more uniform spatial coverage than a rectangular grid
  - also supports the same circular center exclusion zone

### Generator behaviors

- `Append` adds generated targets to the current draft set
- `Replace` discards the current draft set and replaces it with the generated targets
- `Preview` shows generated targets as a temporary dashed overlay in the layout editor before they are committed
- `Apply Preview` commits the current preview using the selected append or replace behavior
- `Clear Preview` discards the current generator preview without changing the draft targets

### Generated target style

`Style Source` controls what style every generated target receives:

- `Explicit style below` (default) applies the radius, hold time, reward channel, enabled
  state, static color/opacity, and active color/opacity set in the generator's
  "Generated Target Style" block to all generated targets. Use this when you want to declare,
  up front, exactly how the whole generated set should look — no need to pre-build a template
  target first.
- `Inherit from selected target` copies those same fields from the currently selected target
  (or the first target if none is selected). This preserves the previous generator behavior.

The style block controls are disabled while `Inherit from selected target` is active.
Generated targets remain fully editable afterward through the table fields and drag-based
layout preview.

### Distinguishing targets in the preview

To keep dense layouts readable, the layout preview draws every target as a hollow ring rather
than a filled disc, so overlapping targets stay individually visible:

- enabled targets are drawn as solid-outline rings in the target color
- disabled targets are drawn as dimmed, dashed rings (rather than only faded fill)
- generated preview targets are dashed blue rings
- target name labels are shown only for the selected target and the target currently under the
  cursor, so labels no longer pile on top of each other when there are many targets

### Layout presets

Named target layouts can be saved and reused from the `Presets` row at the top of the layout
editor:

- `Save` stores the current draft targets under a name you provide (you are prompted before an
  existing preset is overwritten)
- `Load` replaces the current draft with the selected preset (you are prompted before
  discarding a non-empty draft)
- `Append` adds the selected preset's targets to the current draft without replacing anything —
  e.g. layer a COR ring on top of a hex-packing grid. The draft schedule is kept unless you
  tick "Also adopt this preset's schedule" in the confirm dialog. Appended targets whose names
  collide with existing ones are auto-renamed (`Target 1 (COR)`), because schedules resolve
  targets by name.
- `Delete` removes the selected preset

#### Target groups

Targets remember which preset they came from (a `group` field stamped on `Load`/`Append`;
targets that already carry a group — a combined layout re-saved as its own preset — keep it).
When any target has a group, a **Groups** panel appears in the layout editor, and a **Target
Groups** panel appears under the main target table. Each group gets one checkbox
(`hex (8/8 on)`): clicking it enables or disables every target in that group at once. The
panel under the main table edits the **live task config**, so flipping groups mid-session
takes effect on the next trial — combined with the schedule's "auto ring = all enabled
non-center targets" rule, this switches the same screen between (say) hex-random and
center-out behavior without re-authoring the layout. Disabled targets are excluded from the
schedule, from random inserts, and from rendering.

The target tables also have non-destructive `Enable All` / `Disable All` buttons (unlike
`Clear All`, which deletes the targets).

Presets are stored on the task configuration under `target_layout_presets`, so they persist
with the saved config graph.

Presets saved since the schedule feature landed store `{"targets": [...], "schedule": {...}}`;
older presets that are a bare list of targets still load (they get the default random
schedule).

### Structured target schedules (`target_schedule`)

> **Runtime note:** schedules are honored by the **Rust executor only**
> (`joystick_intro_rust` task / `joystick_intro_executor`). The pure-Python `run()` in this
> module intentionally stays a uniform random draw, even when a schedule is configured. The
> shared layout editor writes the config keys either way.

The layout editor's `Schedule` panel controls how the next trial's target is chosen. It is
stored on the task configuration as a single dict:

```json
"target_schedule": {
  "mode": "random",                  // "random" | "sequence" | "center_out"
  "order": ["U1", "R1", "D1", "L1"], // mode=sequence: ordered target names; cycles forever
  "center": "C",                     // mode=center_out: name of the center target
  "peripherals": [],                 // mode=center_out: ring names; empty => all non-center enabled
  "peripheral_order": "sequential",  // "sequential" | "random"
  "interleave_random_ratio": 0.0     // 0..1 prob. a trial is a random draw instead of the scheduled one
}
```

- **Random** (default, and the behavior when the key is absent): uniform random choice over
  enabled targets — identical to the task before schedules existed.
- **Fixed Sequence**: cycles the enabled targets in list order, looping forever. Reorder with
  the `Move Up` / `Move Down` buttons; `order` is written from the list on save.
- **Center-Out**: alternates center → peripheral → center → next peripheral. The center is
  chosen by name from the `Center` dropdown; the peripheral ring is picked in the `Ring` list,
  visited sequentially or at random per `Periphery`.
- **Random insert %** (`interleave_random_ratio`): with this probability a trial is replaced
  by a uniform-random enabled target, and the structured cursor does **not** advance — the
  pattern resumes where it left off. Irrelevant in Random mode.
- In **Center-Out**, the insert roll only happens on a **pair boundary** (when the next
  structured trial would be the center), so a random insert can never split a
  center → peripheral pair: the stream looks like `C → P → R → R → C → P → …`, never
  `C → R → P`. Because only every other slot rolls, the overall fraction of random trials is
  `r / (2 - r)` rather than `r` (e.g. 0.70 ⇒ ~54% of trials are inserts, arriving in runs
  between pairs).

> **Warning:** `Random insert %` at **1.00 disables the structured pattern entirely** — every
> trial rolls the random insert, so center-out never alternates and `Periphery` has no effect.
> This knob is for *occasionally* injecting an off-pattern trial (e.g. `0.20` ≈ 1 in 5), not
> for randomizing the peripheral choice.
>
> **Recipe — center → random target alternation:** mode `Center-Out`, `Random insert %` =
> `0.00`, `Periphery` = `random`. For a fixed ring order use `Periphery` = `sequential`
> instead.

Targets are referenced **by name**. A scheduled name that is missing or disabled falls back to
a random draw for that slot (the schedule keeps advancing, so the pattern resumes on the next
trial).

The schedule's cross-trial position persists in the task config as `_schedule_seq_pos`,
`_schedule_expect_center`, and `_schedule_peripheral_pos` (round-tripped through the Rust
executor's `config_updates`, like `_streak_count`). Saving the layout editor resets them, so
every save restarts the pattern from the top.

Each attempt records which phase selected its target in `behav_result` (see
`schedule_phase` below).

#### The Ring picker (Center-Out)

The `Ring` list in the Schedule panel controls the peripheral set:

- **Auto: all enabled non-center** (checked, the default) means the ring is computed at
  runtime as every enabled non-center target — stored as an **empty `peripherals` list**. The
  greyed, checked items show what Auto resolves to.
- Unchecking Auto makes the ring **explicit**: the list is seeded with the computed ring and
  the checked names are written to `peripherals` (in list order — that is the sequential visit
  order). A target excluded from the ring stays eligible for random-interleave inserts.
- **Check All / Uncheck All** buttons under the list bulk-edit the explicit ring. After
  Uncheck All the editor stays in explicit mode with nothing checked so you can build the ring
  from scratch; an orange hint reminds you that an **empty ring still runs as Auto** at
  runtime (the empty-`peripherals` rule), so check at least one target or group before saving
  if you meant to restrict the ring.
- When targets carry preset **groups**, a row of group checkboxes appears under the ring list
  (`COR (8/8)`): one click includes or excludes every enabled target of that group. This is
  how two appended presets divide labor — e.g. uncheck `hex` so the schedule alternates over
  the COR ring only, while the still-enabled hex targets appear exclusively through random
  inserts.

References are by name and maintained automatically: renaming a target updates
`center`/`peripherals`/`order` (as long as no other target still bears the old name);
deleting a target drops its references. A name that no longer matches an enabled target shows
as `(missing)`/`(disabled)` in the editor and falls back to a random draw at runtime.

#### The Schedule tab

The editor preview has two tabs. **Layout** is the classic draggable canvas of every draft
target. **Schedule** renders only the structured pattern so it cannot be confused with the
random-eligible layout: the center gets a gold `C` badge and a white outline, ring/sequence
targets get numbered badges in visit order (`?` when the ring order is random) with their
names always labeled, and targets not in the schedule are drawn faint. A footer shows the
upcoming pattern (e.g. `C → U1 → C → R1 → …`), the interleave note, and a red warning listing
any referenced targets that are missing or disabled. Clicking a target in either tab selects
it in both.

#### Operator HUD trial tag (Rust executor)

During a session run through `joystick_intro_rust`, the operator window shows a colored tag
over the mirror at each trial start:

- blue `STRUCTURED — center (C)` / `STRUCTURED — peripheral (U1)` / `STRUCTURED — sequence (…)`
- orange `RANDOM insert @30% — pattern resumes (R1)`: the configured `Random insert %`
  rolled; the structured pattern continues on the next slot. Seeing these is the schedule
  working as configured, at roughly the configured rate.
- orange `RANDOM @100% — structured pattern DISABLED (…)`: `Random insert %` is `1.00`, so
  every trial is a random insert and the structured pattern never runs. Lower it (usually to
  `0.00`) if you wanted the structured schedule.
- orange `RANDOM — no schedule in this task config (…)`: the RUNNING task config has no
  structured schedule. Watch for this after editing: queue entries are independent copies of
  the task config, so a queue entry created before you saved the schedule will run without it.
  Re-queue the task (or clear stale queue entries) after authoring a schedule.

This comes from a `TrialInfo=` marker the executor sends on the **operator event stream
only** — it is never written to the Thalamus log and never drawn on the subject display (the
subject render path has no text capability at all).

#### Separating structured vs. random trials in recorded data

Every trial the Rust executor presents is tagged with the schedule phase that selected its
target, in two places:

1. **Per attempt** — `behav_result["attempts"][i]["schedule_phase"]` (see the field reference
   below). Values:
   - `center` — the structured center slot of a Center-Out pair
   - `peripheral` — the structured peripheral slot that completes a pair
   - `sequence` — a structured Fixed-Sequence trial
   - `random` — anything else: Random mode, a random-interleave insert, **or** a structured
     slot that fell back because its scheduled target name was missing/disabled
2. **Per event** — the `target_on` event inside each attempt carries the same
   `schedule_phase`, plus `target_index`, `target_x`, and `target_y`.

Target identity: attempts and `target_on` record `target_index`, which indexes the task
config's `targets` list (names live there — the list order is stable; presets appended into a
layout keep existing rows in place). The `TrialInfo=` HUD marker is operator-stream only and
is **not** in the log — do not look for it in recorded data.

Processing recipes (attempts in presentation order):

```python
attempts = behav_result["attempts"]
structured = [a for a in attempts
              if a.get("schedule_phase") in ("center", "peripheral", "sequence")]
inserts = [a for a in attempts if a.get("schedule_phase") == "random"]
# Center-Out pairs: a center attempt and the attempt immediately after it
pairs = [(a, b) for a, b in zip(attempts, attempts[1:])
         if a.get("schedule_phase") == "center"
         and b.get("schedule_phase") == "peripheral"]
completed_pairs = [(a, b) for a, b in pairs
                   if a.get("outcome") == "success" and b.get("outcome") == "success"]
```

Analysis caveats:

- **Pair adjacency guarantee (sessions recorded on/after 2026-07-07):** in Center-Out mode a
  random insert can only land on a pair boundary, so a `center` attempt is always immediately
  followed by its `peripheral` attempt. **Earlier sessions do not have this guarantee** —
  `random` attempts can sit inside a pair, so pair up phases by searching forward rather than
  assuming adjacency when processing older data.
- **The schedule advances per presented trial, not per success**: a failed `center` attempt is
  still followed by the `peripheral` slot. Filter on `outcome` (as in `completed_pairs`
  above) when you need fully executed center→out reaches.
- The cross-trial cursor (`_schedule_expect_center` etc.) persists in the config between
  runs, so a session can *begin* with a `peripheral` attempt if the previous run stopped
  mid-pair (saving the layout editor resets the cursor).
- `schedule_phase` is **Rust-executor only and additive**: attempts from the pure-Python task
  or from sessions before the schedule feature simply lack the key — treat a missing key as
  `random`-equivalent (uniform draw), not as structured.
- Expected rates in Center-Out: with `Random insert %` = `r`, inserts arrive in runs between
  pairs (geometric, mean `r/(1-r)` per boundary) and make up `r/(2-r)` of all trials
  (`0.70 ⇒ ~54%`). In Sequence mode the fraction is simply `r`.

Automated processing: the py-proc pipeline (`pyCheck/co_structured.py`) implements these recipes
end-to-end. It runs automatically with the day summary (proc_gui's "Generate Day Summary") and
writes a structured-CO reach-path image plus `*_co_structured_attempts.csv` / `*_co_structured_paths.npz`
for reconstructing the paths of structured vs. random trials.

Automated processing: the py-proc repo automates all of the above per day —
`pyCheck/co_structured.py` (run inside "Generate Day Summary" or standalone) writes
`<day>_co_structured.png` (structured/random reach paths + success rates),
`<day>_co_structured_attempts.csv`, and `<day>_co_structured_paths.npz`; see
`pyCheck/README.md` § "Structured center-out outputs".

### Editing conveniences

- the side panel's settings are split into **Target / Generator / Schedule tabs** (the target
  list, presets, and Save/Cancel stay visible at all times), so no section is squeezed into a
  tiny scroll strip
- clicking the generator's `Preview` automatically brings the **Layout** preview tab to the
  front (generated previews are only drawn there)
- most target editor controls now include hover tooltips describing what they change
- the target list inside the layout editor supports multi-selection removal
- the main target table also supports multi-selection removal
- both interfaces now provide a `Clear All` action for deleting the full target set

## Two Kinds of Logging

This task now records behavior at two levels:

1. Canonical task-state events through `BehavState=...`
2. Rich detailed data inside `behav_result`

The `BehavState` stream is intentionally sparse. It is meant to define the basic trial structure for downstream processing.

The rich `behav_result` record keeps the detailed within-trial information that is useful for interpretation and behavioral analysis.

## Canonical `BehavState` Values

These are the only task-structure states that should be treated as the main behavioral state stream:

- `intertrial`
- `start_on`
- `success`
- `fail`

Definitions:

### `intertrial`
The task is between trials.
This is the waiting period before the next target appears.
The task starts in this state.
If `require_center_before_trial` is enabled, the intertrial countdown only runs while the cursor is inside the center gate.

### `start_on`
The target becomes visible.
This is the true start of a target-guided attempt.
This is the event intended to match the photodiode/display change.

### `success`
The attempt ended successfully.
In this task, success means the cursor stayed inside the target long enough to satisfy the hold requirement.

### `fail`
The attempt ended unsuccessfully.
In this task, the most common failure is timeout before successful hold completion.

## High-Level Task Flow

### Normal target-based mode
1. The task begins in `intertrial`.
2. If center-gated trial starts are enabled, the task waits until the cursor is back near center.
3. After the intertrial interval, a target is selected and shown.
4. The task logs `BehavState=start_on`.
5. The subject moves the cursor with the joystick.
6. If the cursor enters the target, the task starts timing the hold.
7. If the cursor stays in long enough, the task succeeds, delivers reward, and logs `BehavState=success`.
8. If the timeout is reached first, the task logs `BehavState=fail`.

## Center-Gated Trial Starts

The `Require Center Before Trial` checkbox adds a return-to-center requirement before a new target can appear.
When enabled, the intertrial interval starts only after the cursor is inside the center gate.
If the cursor leaves the center gate during that interval, the countdown resets and waits for the cursor to return again.

The center gate is controlled by `center_gate_radius_ratio`.
It is a radius around the task-region center, expressed as a fraction of the task region's smaller display dimension.
The default value is `0.15`.

### Free-play mode
1. A single free-play attempt record is created.
2. The task still begins with `BehavState=intertrial`.
3. The subject can move the cursor around without target success/fail logic.
4. If free-play reward is enabled, analog joystick activity can trigger reward.
5. When the configured end key is released, the run ends as a successful free-play exit.

## Touch-Input Failure

The `Fail On Touch Input` checkbox can be used to discourage reaching to the touch screen during joystick shaping.
When enabled, any valid touch-screen input during cursor-only free play fails the free-play run, plays the failure sound, and logs `BehavState=fail`.

In normal target-guided mode, touch input fails the active target attempt.
Touches during intertrial are ignored so the punishment is tied to the visible joystick trial.

Touch failures are stored with `failure_reason` set to `touch_input`.

## Cursor-Only Free-Play Reward

Cursor-only free-play mode can optionally reward joystick exploration. This is intended for the earliest learning stage, where simply contacting or moving the joystick should be reinforced before target acquisition is required.

This reward path is only used when `cursor_only_mode` is `true`. Normal target-based trials are unchanged.

Reward channels in this task are looked up through the shared task-controller reward schedule. See [Reward Schedule Configuration](reward_schedule_configuration.md) for how schedule files, channel indexes, and reward durations are currently wired together.

### `reward_scale`

The reward channel still selects a base pulse duration (ms) from the shared reward schedule, exactly as every other task expects. This task then applies `reward_scale`, a continuous multiplier, to that base duration just before the reward pulse is injected:

```
effective_ms = round(base_ms * reward_scale)
```

This lets reward be ramped in fine, sub-channel steps (e.g. dialing `1.000 -> 0.950 -> 0.900` against a 600 ms base gives 30 ms decrements) instead of jumping a whole channel. `reward_scale` defaults to `1.0`, which reproduces the original behavior exactly, and it is local to this task only — the shared `reward_schedule`, `get_reward`, and channel layout are unchanged, so other tasks are unaffected. The multiplier applies to every reward this task delivers (trial success, streak bonus, and free-play). The value used is recorded on `reward_triggered`/`bonus_reward_triggered` events for the trial log.

### Free-play reward settings

The cursor-only free-play controls are grouped together in the task UI. The enable checkbox is always visible in that group, and the end-key plus reward controls are shown underneath it when cursor-only free play is enabled.

### `free_play_active_threshold`
Analog joystick magnitude needed to count as active for free-play reward.
The UI presents this as a slider from `0.00` to `1.00`.
If this is `0.0`, the task uses the same movement threshold as the zero-drift setting.

### First-touch reward

First-touch reward is controlled by:
- `free_play_first_touch_reward_enabled`
- `free_play_first_touch_reward_channel`

When enabled, the task rewards the first inactive-to-active joystick transition in the free-play attempt.
This is useful for shaping initial joystick contact.

### Bout-start reward

Bout-start reward is controlled by:
- `free_play_bout_reward_enabled`
- `free_play_bout_reward_channel`
- `free_play_bout_cooldown_s`

When enabled, the task rewards each inactive-to-active joystick transition, subject to the cooldown.
This is useful for shaping repeated re-engagement with the joystick.

### Sustained-active reward

Sustained-active reward is controlled by:
- `free_play_sustain_reward_enabled`
- `free_play_sustain_reward_channel`
- `free_play_sustain_initial_delay_s`
- `free_play_sustain_interval_s`

When enabled, the task rewards repeatedly while the analog joystick remains active.
The initial delay controls how long the joystick must remain active before the first sustained reward, and the interval controls repeated rewards after that.
This is useful for shaping continued joystick holding or manipulation.

### Important free-play reward note

Free-play reward uses the raw analog joystick input, not the operator keyboard arrow-key override. This keeps the reward tied to physical joystick contact or movement.
Each triggered free-play reward also plays the task success sound as an immediate audio cue.

## Control Modes

The task supports two cursor control styles.

### `direct`
Joystick position directly maps to cursor position around the center.
This feels more like moving a handle to place the cursor.

### `cumulative`
Joystick acts like velocity input.
Pushing the joystick causes the cursor to keep moving over time.
This feels more like steering or moving a mouse.

## What Is Stored at Session Level

The task stores a `behav_result` dictionary for the whole run.

### `task`
Name of the task.
For this file it is `"joystick_intro"`.

### `control_mode`
The control mode used for the run.
Usually `direct` or `cumulative`.

### `cursor_only_mode`
Whether the task ran in free-play cursor-only mode.
If `true`, there is no normal target-guided trial sequence.

### `trial_attempt_count`
Number of attempt records stored in `attempts`.
This increases each time an attempt is finalized and appended.

### `attempts`
A list of per-attempt records.
Each attempt contains detailed trial information and rich within-trial events.

### `joystick_samples`
Continuous joystick data collected across the session.
Each sample stores:
- `time_perf_counter`
- `time_since_session_start_s`
- `x`
- `y`

This log is separate from the event stream.
It is intended for later alignment and movement analysis.

### `session_start_perf_counter`
Task-side timestamp marking the start of the session.
This is based on Python performance-counter time.

### `final_outcome`
Outcome of the last finalized attempt.
Examples include `success`, `fail`, or `ignored_idle`.

### `final_attempt`
A copy of the last finalized attempt record.
This is a convenience field so downstream code can access the final attempt quickly.

## What Is Stored Per Attempt

Each normal trial attempt gets its own dictionary inside `behav_result["attempts"]`.
In free-play mode, one attempt is created at free-play start.

### `attempt_index`
Sequential counter for each attempt recorded during the task run.
Starts at 1 and increments every time a new attempt record is created.
In normal task mode, a new attempt starts when a target is turned on.
In free-play mode, a single attempt is created at free-play start.

### `start_time_perf_counter`
Task-side timestamp for the start of that attempt.
In normal mode, this is when the target-guided attempt is created just before target onset is logged.
In free-play mode, it is the free-play start time.

### `control_mode`
The cursor control style used during that attempt.
Usually `direct` or `cumulative`.

### `cursor_only_mode`
Whether that attempt belongs to free-play mode rather than normal target-guided mode.

### `target_index`
The index of the chosen target within `task_config["targets"]`.
Only enabled targets are eligible to be selected.
If fallback target placement is used because no enabled targets exist, this is `-1`.

### `target_position`
A dictionary with normalized coordinates:
`{"x_norm": ..., "y_norm": ...}`
These are the target center coordinates in task-region coordinates, not screen pixels.
`x_norm` and `y_norm` each range from `0.0` to `1.0`.

### `target_radius_ratio`
The radius of the target in normalized task-region units.
It is the ratio later multiplied by the minimum task-region dimension in pixels to draw the target.
This is not a pixel radius.

### `hold_time_s`
Required time in seconds that the cursor must remain continuously inside the target to succeed.
Pulled from the selected target configuration at target onset.

### `reward_channel`
Reward channel selected for that target on that attempt.
If an older target config does not provide this field, the task falls back to the top-level `reward_channel`.
In cursor-only free-play mode, target reward fields are not used; free-play reward fields describe exploratory reward instead.

### `target_color_rgb`
The static target color selected at target onset.
Stored as `[R, G, B]`, integer values from `0` to `255`.

### `target_opacity`
The static target opacity selected at target onset.
Stored from `0.0` fully transparent to `1.0` fully opaque.

### `target_active_color_rgb`
The active target color used when the cursor is inside the target.
Stored as `[R, G, B]`, integer values from `0` to `255`.

### `target_active_opacity`
The active target opacity used when the cursor is inside the target.
Stored from `0.0` fully transparent to `1.0` fully opaque.

### `events`
List of rich within-attempt events.
These are more detailed than `BehavState` and are useful for behavioral analysis.

### `joystick_active`
Whether meaningful joystick movement occurred during the attempt.
This becomes `true` once joystick magnitude crosses the movement threshold used by the task.

### `target_entry_count`
How many times the cursor entered the target during the attempt.
This increases on each outside-to-inside transition.

### `outcome`
Final labeled outcome for that attempt.
Typical values are `success`, `fail`, or `ignored_idle`.

### `failure_reason`
More specific explanation when an attempt does not end normally.
Examples:
- `timeout_without_movement`
- `timeout_after_movement`

### `schedule_phase`
Which schedule phase selected this attempt's target: `random`, `sequence`, `center`, or
`peripheral`. Also attached to the `target_on` event.

**Rust executor only** — this key is additive and only emitted by the Rust executor
(`joystick_intro_rust`); the pure-Python `run()` never writes it. See
"Structured target schedules" above, and "Separating structured vs. random trials in
recorded data" for pairing recipes and analysis caveats (pair adjacency, failed-trial
advancement, cross-run cursor persistence).

### `end_time_perf_counter`
Task-side timestamp for the end of the attempt.
Added when the attempt is finalized.

### `duration_s`
Duration of the attempt in seconds.
Computed as end time minus start time.

### `first_movement_time_s`
Seconds from attempt start to the first detected joystick movement above threshold.
If no meaningful movement happened, this is `None`.

### `first_target_entry_time_s`
Seconds from attempt start to the first entry into the target.
If the cursor never entered the target, this is `None`.

### `first_hold_start_time_s`
Seconds from attempt start to the first time a target hold began.
If no hold ever began, this is `None`.

### `success_time_s`
For successful attempts, this matches the attempt duration.
For non-successful attempts, this is `None`.

### `free_play_active_threshold`
Only used in cursor-only free-play mode.
Joystick magnitude threshold used to count analog joystick activity.

### `free_play_first_touch_reward_enabled`
Only used in cursor-only free-play mode.
Whether first-touch reward was enabled for the attempt.

### `free_play_first_touch_reward_channel`
Only used in cursor-only free-play mode.
Reward channel used for first-touch reward.

### `free_play_bout_reward_enabled`
Only used in cursor-only free-play mode.
Whether bout-start reward was enabled for the attempt.

### `free_play_bout_reward_channel`
Only used in cursor-only free-play mode.
Reward channel used for bout-start rewards.

### `free_play_bout_cooldown_s`
Only used in cursor-only free-play mode.
Minimum seconds between bout-start rewards.

### `free_play_sustain_reward_enabled`
Only used in cursor-only free-play mode.
Whether sustained-active reward was enabled for the attempt.

### `free_play_sustain_reward_channel`
Only used in cursor-only free-play mode.
Reward channel used for sustained-active rewards.

### `free_play_sustain_initial_delay_s`
Only used in cursor-only free-play mode.
Seconds of continuous activity required before sustained-active reward begins.

### `free_play_sustain_interval_s`
Only used in cursor-only free-play mode.
Seconds between sustained-active reward requests while the joystick remains active.

### `free_play_first_touch_reward_count`
Only used in cursor-only free-play mode.
Number of first-touch rewards triggered during the free-play attempt.

### `free_play_bout_reward_count`
Only used in cursor-only free-play mode.
Number of bout-start rewards triggered during the free-play attempt.

### `free_play_sustain_reward_count`
Only used in cursor-only free-play mode.
Number of sustained-active rewards triggered during the free-play attempt.

### `free_play_total_reward_count`
Only used in cursor-only free-play mode.
Total exploratory rewards triggered during the free-play attempt.

### `free_play_active_bout_count`
Only used in cursor-only free-play mode.
Number of inactive-to-active joystick bouts detected during the free-play attempt.

### `free_play_total_active_time_s`
Only used in cursor-only free-play mode.
Total time the analog joystick was active across completed active bouts.

## Rich Event Names Stored in `events`

These are the detailed events currently used inside each attempt record.

### `target_on`
The target became visible for that attempt.
This is the detailed event version of the trial start.
The canonical task-state equivalent is `BehavState=start_on`.

### `first_joystick_movement`
The first meaningful joystick movement detected during that attempt.
Useful for movement-latency measures.

### `target_entry`
The cursor crossed from outside the target to inside the target.
Logged each time the cursor enters.

### `target_exit`
The cursor crossed from inside the target to outside the target.
Useful for re-entry and overshoot analysis.

### `hold_start`
The cursor is inside the target and hold timing begins.

### `hold_break`
The cursor left the target after a hold had started but before hold completion.

### `hold_complete`
The hold requirement was satisfied.
This is the event that immediately precedes reward delivery and successful trial completion.

### `reward_triggered`
The standard reward was requested.
The event includes the `reward_channel` used for that reward request.

### `bonus_reward_triggered`
An additional bonus reward was requested because a configured streak threshold was reached.
The event includes the `reward_channel` used for those reward requests.

### `ignored_idle_timeout`
The trial timed out without meaningful movement, and the task was configured to ignore idle failures.
This attempt is saved, but the task returns to `intertrial` instead of producing a canonical `fail`.
The event includes the current consecutive ignored-idle trial count.

### Prolonged ignored-idle sample clearing

When `ignore_idle_trial_failures` is enabled, the task may stay in the same run across many unattended idle trials.
Raw joystick samples are collected throughout the run, so a prolonged idle period can make the final trial summary too large for the gRPC log message limit.

`ignored_idle_sample_clear_threshold` controls when the task trims the raw joystick sample buffer during consecutive ignored-idle trials.
The default is `50`.
When the consecutive ignored-idle count is greater than this threshold, `joystick_samples` is cleared after each ignored-idle attempt.
The task still keeps the ignored-idle attempt records and stores a summary entry in `ignored_idle_sample_clear_events`.

Each clear event stores:
- task-side clear time
- seconds since session start
- consecutive ignored-idle trial count
- number of raw joystick samples cleared

### `free_play_start`
Marks the start of the free-play attempt.
Only used in cursor-only free-play mode.

### `free_play_end_requested`
Marks the end-key release that requests free-play exit.
Only used in cursor-only free-play mode.

### `free_play_active_start`
The analog joystick crossed from inactive to active.
Only used in cursor-only free-play mode.

### `free_play_active_end`
The analog joystick crossed from active to inactive.
Only used in cursor-only free-play mode.
The event includes the bout duration and total active time so far.

### `free_play_first_touch_reward_triggered`
A first-touch exploratory reward was requested.
Only used in cursor-only free-play mode.

### `free_play_bout_reward_triggered`
A bout-start exploratory reward was requested.
Only used in cursor-only free-play mode.

### `free_play_sustain_reward_triggered`
A sustained-active exploratory reward was requested.
Only used in cursor-only free-play mode.
Free-play reward events include reward channel, reward count, total free-play reward count, joystick x/y values, joystick magnitude, and reward kind.

### `touch_input_fail`
Touch-screen input was detected while touch failure was enabled.
The event includes the touch x/y position and task-side touch timestamp.
In normal target-guided mode this fails the active target attempt.
In cursor-only free-play mode this fails the free-play run.

## What Is Stored Per Joystick Sample

Each item in `behav_result["joystick_samples"]` contains:

### `time_perf_counter`
Raw task-side timestamp from the Python performance counter.
Useful for alignment with other task-side times.

### `time_since_session_start_s`
Seconds since the task session began.
This is easier for plotting and within-session comparisons.

### `x`
Joystick X value for that sample.

### `y`
Joystick Y value for that sample.

## What Metrics Are Tracked Directly or Can Be Derived

The task already supports several useful behavioral measures.
Some are explicit fields, and others can be calculated from the saved events and joystick samples.

### Metrics directly supported now
- Trial outcome
- Trial duration
- Attempt count
- First movement latency
- First target-entry latency
- First hold-start latency
- Hold completion timing
- Number of target entries
- Reward and bonus-reward occurrence
- Free-play exploratory reward occurrence
- Whether a timeout happened before or after movement
- Whether an idle timeout was ignored
- Session-wide joystick trajectory over time

### Metrics that can be derived with current logs
- Target onset time aligned to photodiode using `BehavState=start_on`
- Acquisition time from `target_on` to first `target_entry`
- Number of re-entries using repeated `target_entry` and `target_exit`
- Approximate overshoot using entry/exit patterns before success
- Movement initiation timing using `first_joystick_movement`
- Directional performance by comparing target location and outcome across attempts
- Streak-related performance using success history and reward events

### Metrics not fully supported unless more data are added
The task does not currently store continuous cursor samples, only joystick samples.
Because of that, some path-based metrics are limited or mode-dependent.
These include:
- precise cursor path length
- precise path efficiency
- cursor peak speed and average speed
- time in target versus out of target from continuous position traces
- endpoint variability at hold onset from continuous cursor samples
- hold stability based on cursor jitter while inside the target

## Important Timing Notes

### Photodiode alignment
`BehavState=start_on` is the main trial-start event intended to match the visible target onset.
If the photodiode square changes on that same display update, this is the best event for display-aligned timing.

### Task-side timestamps
All saved timestamps in `behav_result` use Python performance-counter time.
That makes them internally consistent within the task.
For alignment to external recordings, the usual strategy is to anchor the trial using the photodiode-linked `start_on` event.

## Important Interpretation Notes

### Normal mode versus free-play mode
Normal mode is target-guided and uses success/fail logic.
Free-play mode is exploratory and ends on key release.
Some fields are therefore more meaningful in normal mode than in free-play mode.

### Ignored idle trials
If `ignore_idle_trial_failures` is enabled, a trial that times out without meaningful movement is still stored as an attempt with outcome `ignored_idle`.
However, the canonical `BehavState` stream returns to `intertrial` instead of emitting `fail` for that case.

### Target coordinates are normalized
Target coordinates and target size are stored in normalized task-region units, not screen pixels.
This is helpful because it makes the saved values independent of monitor resolution.

## In One Sentence

This task teaches joystick-to-cursor control, presents a target, requires a continuous hold for success, logs a simple trial-state stream for downstream alignment, and saves detailed attempt-by-attempt behavioral information plus continuous joystick samples for later analysis.
