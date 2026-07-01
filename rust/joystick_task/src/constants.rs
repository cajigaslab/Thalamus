//! Per-target and input constants mirrored from
//! `thalamus/task_controller/joystick_intro.py` (module-level, lines 41-49).
//! These are NOT in the JSON config; they are the built-in defaults the Python
//! code uses when a target dict omits a field. Keep in sync with that module.

/// Default target radius as a ratio of the task region (joystick_intro.py:41).
pub const DEFAULT_TARGET_RADIUS_RATIO: f64 = 0.08;

/// Default target fill color RGB (joystick_intro.py:42).
pub const DEFAULT_TARGET_COLOR: [u8; 3] = [0, 220, 60];

/// Default hold duration in seconds to acquire a target (joystick_intro.py:46).
pub const DEFAULT_TARGET_HOLD_TIME: f64 = 0.40;

/// Keyboard operator-override joystick magnitude (joystick_intro.py:47).
pub const KEYBOARD_JOYSTICK_MAGNITUDE: f64 = 1.0;

/// Default cap on retained joystick samples in behav_result (joystick_intro.py:49).
pub const DEFAULT_MAX_LOGGED_JOYSTICK_SAMPLES: i64 = 2000;

/// Photodiode/state-indicator square edge length in pixels (joystick_intro.py:2829,
/// `state_width = 70`). Position is offset from the bottom-right corner by the
/// configurable `state_indicator_x` / `state_indicator_y`.
pub const STATE_INDICATOR_EDGE_PX: u32 = 70;
