# Joystick Paddle Intro Task Guide

This guide explains what `joystick_paddle_intro.py` is doing in plain language and describes the information it records during a run.

## What This Task Is For

The joystick paddle intro task teaches the subject to control a vertical paddle and intercept an incoming ball.

This is intended as a bridge between:
- simple joystick-to-cursor agency
- continuous joystick control
- Pong-like interception behavior

In normal task mode:
- The task waits between trials.
- During the intertrial period, the ball is not shown.
- A ball appears on the right side of the task region.
- The subject controls a paddle on the left side of the task region.
- The ball moves toward the paddle.
- If the paddle overlaps the ball at the right time, the trial succeeds and reward is delivered.
- If the ball passes the paddle first, the trial fails.
- If the trial times out first, the trial fails.

If idle-failure ignoring is enabled:
- Trials with no meaningful joystick movement can be saved as `ignored_idle`.
- In that case the task returns to `intertrial` instead of ending with canonical `fail`.

## Two Kinds of Logging

This task records behavior at two levels:

1. Canonical task-state events through `BehavState=...`
2. Rich detailed data inside `behav_result`

The `BehavState` stream is intentionally sparse. It defines the basic trial structure for downstream processing.

The richer `behav_result` record keeps more detailed within-trial information that is useful for behavioral analysis.

## Canonical `BehavState` Values

These are the canonical task-structure states for this task:

- `intertrial`
- `start_on`
- `success`
- `fail`

Definitions:

### `intertrial`
The task is between trials.
This is the waiting period before the next ball appears.
The task starts in this state.
During this period, the ball is not drawn on the screen.

### `start_on`
The ball becomes visible and starts moving.
This is the true start of the interception attempt.

### `success`
The attempt ended successfully.
In this task, success means the paddle intercepted the ball.

### `fail`
The attempt ended unsuccessfully.
In this task, the most common failure is a miss or a timeout after movement.

## High-Level Task Flow

### Normal interception mode
1. The task begins in `intertrial`.
2. After the intertrial interval, a new ball is spawned.
3. The task logs `BehavState=start_on`.
4. The subject moves the paddle with the joystick.
5. The ball moves toward the paddle.
6. If the paddle intercepts the ball, the task succeeds, delivers reward, and logs `BehavState=success`.
7. If the ball passes the paddle first, the task logs `BehavState=fail`.
8. If the timeout is reached first, the task logs `BehavState=fail`.

### Idle-ignored trials
1. The task begins a normal interception attempt.
2. No meaningful joystick movement occurs.
3. The ball is missed or the timeout is reached.
4. If `ignore_idle_trial_failures` is enabled, the attempt is saved as `ignored_idle`.
5. The task returns to `intertrial` instead of emitting canonical `fail`.

## Control Modes

The task supports two paddle control styles.

### `direct`
Joystick Y position directly maps paddle Y position around the center.
This feels more like placing the paddle directly.

### `cumulative`
Joystick Y acts like velocity input.
Pushing the joystick moves the paddle over time.
This feels more like steering.

## Ball Motion and Wall Bounces

Each trial contains one ball.

The ball:
- starts on the right side of the task region
- moves left toward the paddle
- may have vertical drift
- may optionally bounce off the top and bottom walls

If `ball_bounce_off_walls` is enabled:
- the task checks whether the ball reaches the top or bottom boundary
- the ball Y position is clamped back to the boundary
- the sign of vertical velocity is flipped

If `ball_bounce_off_walls` is disabled:
- the ball Y position is simply clamped inside the allowed range
- no bounce occurs

## Operator Keyboard Testing

This task supports operator keyboard control for testing.

- `Up` moves the paddle upward
- `Down` moves the paddle downward
- `Left` and `Right` key states are still tracked for consistency with the joystick intro task

The keyboard path is intended for debugging and shaping without requiring joystick hardware input.

## What Is Stored at Session Level

The task stores a `behav_result` dictionary for the whole run.

### `task`
Name of the task.
For this file it is `"joystick_paddle_intro"`.

### `control_mode`
The control mode used for the run.
Usually `direct` or `cumulative`.

### `trial_attempt_count`
Number of finalized attempt records stored in `attempts`.

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

Each trial attempt gets its own dictionary inside `behav_result["attempts"]`.

### `attempt_index`
Sequential counter for each attempt recorded during the task run.
Starts at 1 and increments each time a new attempt record is created.

### `start_time_perf_counter`
Task-side timestamp for the start of that attempt.
In this task, it is recorded when the ball turns on for a new trial.

### `control_mode`
The paddle control style used during that attempt.

### `paddle_x_norm`
The normalized horizontal location of the paddle.
This is fixed for the trial and stored so downstream analysis can reconstruct geometry.

### `paddle_height_ratio`
The normalized paddle height used for that attempt.

### `ball_radius_ratio`
The normalized ball radius used for that attempt.

### `ball_speed_norm_per_s`
The horizontal ball speed configured for that attempt.

### `ball_start_position`
A dictionary with normalized coordinates:
`{"x_norm": ..., "y_norm": ...}`

### `ball_velocity`
A dictionary with normalized velocity components:
`{"x_norm_per_s": ..., "y_norm_per_s": ...}`

### `events`
List of rich within-attempt events.

### `joystick_active`
Whether meaningful joystick movement occurred during the attempt.

### `outcome`
Final labeled outcome for that attempt.
Typical values are `success`, `fail`, or `ignored_idle`.

### `failure_reason`
More specific explanation when an attempt does not end normally.
Examples:
- `missed_ball_without_movement`
- `missed_ball_after_movement`
- `timeout_without_movement`
- `timeout_after_movement`

### `end_time_perf_counter`
Task-side timestamp for the end of the attempt.

### `duration_s`
Duration of the attempt in seconds.

### `first_movement_time_s`
Seconds from attempt start to the first detected joystick movement above threshold.
If no meaningful movement happened, this is `None`.

## Rich Event Names Stored in `events`

These are the detailed events currently used inside each attempt record.

### `ball_on`
The ball became visible and began moving for that attempt.
This is the detailed event version of trial start.
The canonical task-state equivalent is `BehavState=start_on`.

### `first_joystick_movement`
The first meaningful joystick movement detected during that attempt.
Useful for movement-latency measures.

### `intercept`
The paddle intercepted the ball.
This marks successful overlap between paddle and ball.

### `reward_triggered`
The reward was requested.
The event includes the `reward_channel` used for that reward request.

### `success`
Marks successful completion of the trial after interception.

### `miss`
The ball passed the paddle without interception.
This event includes paddle position, ball position, and the failure reason.

### `fail`
The attempt timed out before success.

### `wall_bounce_top`
The ball hit the top boundary and bounced.
Only logged when wall bouncing is enabled and a top-wall collision occurs.

### `wall_bounce_bottom`
The ball hit the bottom boundary and bounced.
Only logged when wall bouncing is enabled and a bottom-wall collision occurs.

## What Is Stored Per Joystick Sample

Each item in `behav_result["joystick_samples"]` contains:

### `time_perf_counter`
Raw task-side timestamp from the Python performance counter.

### `time_since_session_start_s`
Seconds since the task session began.

### `x`
Joystick X value for that sample.
This task does not currently use X for paddle control, but it is still recorded from the input stream.

### `y`
Joystick Y value for that sample.
This is the axis currently used for paddle control.

## What Metrics Are Tracked Directly or Can Be Derived

The task already supports several useful behavioral measures.

### Metrics directly supported now
- Trial outcome
- Trial duration
- Attempt count
- First movement latency
- Reward occurrence
- Miss occurrence
- Whether a miss happened before or after meaningful movement
- Ball start position
- Ball velocity
- Wall-bounce occurrence when enabled

### Metrics that can be derived from saved data
- Paddle reaction time
- Interception success rate
- Miss rate by ball spawn height
- Miss rate by ball drift direction
- Performance with and without wall bounces
- Relationship between joystick trajectory and interception timing
- Learning across attempts or sessions

## Key Config Parameters

Some of the most important task parameters are:

### `control_mode`
Chooses direct or cumulative paddle control.

### `paddle_x_norm`
Sets the horizontal position of the paddle within the task region.

### `paddle_height_ratio`
Controls interception difficulty by changing paddle size.

### `ball_speed_norm_per_s`
Controls how fast the ball moves left toward the paddle.

### `ball_vertical_drift_norm_per_s`
Controls how much vertical motion the ball can have.
Larger values make tracking and prediction harder.

### `ball_spawn_y_min` and `ball_spawn_y_max`
Define the vertical range where new balls can start.

### `ball_bounce_off_walls`
Enables or disables wall-bounce trials.

### `trial_timeout`
Sets the maximum duration of a trial.

### `ignore_idle_trial_failures`
Controls whether no-movement misses and no-movement timeouts produce canonical failure or recycle quietly back to `intertrial`.

## Practical Training Use

This task is well suited as a pre-Pong shaping step because it teaches:
- paddle ownership
- one-dimensional control
- online tracking
- timing-sensitive interception
- optional trajectory prediction through wall bounces

A practical progression is:
1. Slow balls, large paddle, no wall bounces
2. Faster balls, smaller paddle, vertical drift
3. Wall bounces enabled
4. More varied trajectories
5. Multi-hit or rally-based tasks later
