# Joystick Intro Task Guide

This guide explains what `joystick_intro.py` is doing in plain language and describes the information it records during a run.

## What This Task Is For

The joystick intro task teaches the subject that moving the joystick controls a visible cursor on the screen.

In normal task mode:
- The task waits between trials.
- A target appears.
- The subject moves the cursor into the target.
- The cursor must stay inside the target for a required hold time.
- If the hold is completed, the trial succeeds and reward is delivered.
- If the time limit expires first, the trial fails.

In free-play mode:
- No target-based trial logic is used.
- The subject can move the cursor freely.
- The task ends when the configured key is released.

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
2. After the intertrial interval, a target is selected and shown.
3. The task logs `BehavState=start_on`.
4. The subject moves the cursor with the joystick.
5. If the cursor enters the target, the task starts timing the hold.
6. If the cursor stays in long enough, the task succeeds, delivers reward, and logs `BehavState=success`.
7. If the timeout is reached first, the task logs `BehavState=fail`.

### Free-play mode
1. A single free-play attempt record is created.
2. The task still begins with `BehavState=intertrial`.
3. The subject can move the cursor around without target success/fail logic.
4. When the configured end key is released, the run ends as a successful free-play exit.

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

### `target_color_rgb`
The displayed target color at target onset.
Stored as `[R, G, B]`, integer values from `0` to `255`.

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

### `free_play_start`
Marks the start of the free-play attempt.
Only used in cursor-only free-play mode.

### `free_play_end_requested`
Marks the end-key release that requests free-play exit.
Only used in cursor-only free-play mode.

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
