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
- `Use selected target style` copies radius, hold time, reward channel, enabled state, static style, and active style from the selected target when generating new targets
- generated targets are still editable afterward through the same table fields and drag-based layout preview

### Editing conveniences

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
