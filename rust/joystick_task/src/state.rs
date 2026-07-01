//! Joystick_intro state machine (PORT TARGET — M3).
//!
//! This is the heart of the behavioral parity work: a faithful port of the
//! Python run() main loop (joystick_intro.py:2881-3235). It is intentionally a
//! stub right now; the foundation modules (config, input, clock, grpc, reward,
//! events) exist so this can be filled in without further plumbing.
//!
//! What to port here, in order:
//!   1. State enum: Intertrial, StartOn, plus the free-play (cursor_only_mode)
//!      branch. Python uses a bare string `state` (joystick_intro.py:2127).
//!   2. Cursor integration: direct vs cumulative modes with zero-drift and
//!      direction influence (joystick_intro.py:2886-2919), using config::Influence.
//!   3. Transitions + timing: ITI -> start_on (target shown), hold logic, timeout
//!      fail, ignored-idle re-arm.
//!   4. On each transition: emit the BehavState marker (Rust logs to Thalamus.log
//!      AND streams a TrialEvent::Marker), toggle the photodiode brightness
//!      (joystick_intro.py:3081), and append_event into the current Attempt.
//!   5. Rewards via reward::deliver_reward using TrialConfig.reward_ms.
//!   6. Build behav_result (events.rs) and return the terminal success bool.
//!
//! The loop runs at the DISPLAY frame rate (240 Hz) driven by render::run_loop,
//! NOT the Python 100 Hz sleep — that is the whole point of the patch.

use crate::clock::ClockMap;
use crate::config::TaskConfig;
use crate::events::BehavResult;
use crate::input::JoystickState;

/// Behavioral state (mirrors the Python string values for log/marker parity).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    Intertrial,
    StartOn,
    /// Free-play / cursor_only_mode branch.
    CursorOnly,
}

impl State {
    /// The exact string used in "BehavState=..." markers.
    pub fn marker(&self) -> &'static str {
        match self {
            State::Intertrial => "intertrial",
            State::StartOn => "start_on",
            State::CursorOnly => "cursor_only",
        }
    }
}

/// Outcome of a single trial (drives TrialEvent::Success and reward-schedule
/// advance on the Python side).
#[derive(Debug, Clone, Copy)]
pub struct TrialOutcome {
    pub success: bool,
}

/// Per-trial mutable state. Fields will grow during the M3 port.
pub struct Trial {
    pub config: TaskConfig,
    pub clock: ClockMap,
    pub joystick: JoystickState,
    pub behav: BehavResult,
    // cursor_x/y, state, hold_start, iti_end, streak, etc. — added during M3.
}

impl Trial {
    pub fn new(config: TaskConfig, clock: ClockMap, joystick: JoystickState) -> Self {
        Self {
            config,
            clock,
            joystick,
            behav: BehavResult::default(),
        }
    }

    /// Advance one frame. Returns Some(outcome) when the trial terminates.
    /// TODO(M3): implement the full state machine.
    pub fn step(&mut self, _dt_s: f64) -> Option<TrialOutcome> {
        unimplemented!("M3: port joystick_intro.py:2881-3235 state machine")
    }
}
