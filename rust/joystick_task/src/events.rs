//! Behavioral event / `behav_result` model.
//!
//! Byte-compatibility target: after `json.loads`/`json.dumps` normalization, the
//! `behav_result` this produces must be identical to the Python one so downstream
//! analysis (record_reader) is unaffected. Two rules:
//!   1. KEY ORDER must match Python's dict insertion order. serde serializes struct
//!      fields in declaration order, so field order below mirrors Python exactly.
//!   2. NULLABLE fields must emit `null` (Python emits None), so we use Option<T>
//!      WITHOUT skip_serializing_if.
//!
//! Sources:
//!   - reset_attempt_tracking  (joystick_intro.py:2388-2407)  -> Attempt initial fields
//!   - finalize_attempt        (joystick_intro.py:2420-2437)  -> Attempt terminal fields
//!   - append_event            (joystick_intro.py:2409-2418)  -> Event
//!   - joystick sample bookkeeping (joystick_intro.py:2280-2297) -> BehavResult tail
//!
//! STATUS: data model only. The state machine that POPULATES these lands in M3
//! (state.rs). Field order/extras must be pinned against golden Python output in
//! M6 (see docs/rust_bci_patch.md "Parity / verification").

use indexmap::IndexMap;
use serde::Serialize;
use serde_json::Value;

/// One behavioral event within an attempt.
/// `name`, `time_perf_counter`, `time_since_attempt_start_s` come first (Python
/// builds them in that order), then any per-call-site extras via `extra`.
#[derive(Debug, Clone, Serialize)]
pub struct Event {
    pub name: String,
    pub time_perf_counter: f64,
    pub time_since_attempt_start_s: f64,
    /// Per-call-site keyword extras (append_event(**extra)). Order-preserving.
    #[serde(flatten)]
    pub extra: IndexMap<String, Value>,
}

/// One trial attempt. Field order matches the Python dict literal exactly.
#[derive(Debug, Clone, Serialize)]
pub struct Attempt {
    pub attempt_index: i64,
    pub start_time_perf_counter: f64,
    pub control_mode: String,
    pub cursor_only_mode: bool,
    pub target_index: Option<i64>,
    pub target_position: Option<[f64; 2]>,
    pub target_radius_ratio: Option<f64>,
    pub hold_time_s: Option<f64>,
    pub reward_channel: Option<i64>,
    pub target_color_rgb: Option<[i64; 3]>,
    pub target_opacity: Option<f64>,
    pub target_active_color_rgb: Option<[i64; 3]>,
    pub target_active_opacity: Option<f64>,
    pub events: Vec<Event>,
    pub joystick_active: bool,
    pub target_entry_count: i64,
    pub outcome: Option<String>,
    pub failure_reason: Option<String>,
    // --- fields appended by finalize_attempt (only present after finalization) ---
    // Python adds these keys AFTER the initial dict, so they serialize last.
    pub end_time_perf_counter: Option<f64>,
    pub duration_s: Option<f64>,
    pub first_movement_time_s: Option<f64>,
    pub first_target_entry_time_s: Option<f64>,
    pub first_hold_start_time_s: Option<f64>,
    pub success_time_s: Option<f64>,
}

/// Top-level behav_result. `attempts`/`trial_attempt_count`/`final_outcome`/
/// `final_attempt` come from finalize_attempt; the joystick_* tail from the
/// sample bookkeeping. Kept as a builder so the state machine appends as it goes.
#[derive(Debug, Clone, Default, Serialize)]
pub struct BehavResult {
    pub attempts: Vec<Attempt>,
    pub trial_attempt_count: i64,
    pub final_outcome: Option<String>,
    pub final_attempt: Option<Attempt>,
    // Joystick sample bookkeeping (joystick_intro.py:2280-2297).
    pub joystick_samples: Vec<Value>,
    pub joystick_sample_count: i64,
    pub joystick_samples_dropped: i64,
    pub joystick_samples_kept: i64,
}

impl BehavResult {
    /// Serialize to a JSON string for TrialEvent.behav_result_json. The delegate
    /// hands this to Python which sets context.behav_result unchanged.
    pub fn to_json(&self) -> anyhow::Result<String> {
        Ok(serde_json::to_string(self)?)
    }

    /// Append a finalized attempt and update the summary fields, matching
    /// finalize_attempt (joystick_intro.py:2433-2437).
    pub fn push_attempt(&mut self, attempt: Attempt) {
        let outcome = attempt.outcome.clone();
        self.attempts.push(attempt.clone());
        self.trial_attempt_count = self.attempts.len() as i64;
        self.final_outcome = outcome;
        self.final_attempt = Some(attempt);
    }
}

// TODO(M3): the append_event / reset_attempt_tracking flow and the exact `extra`
// payloads at each of the ~19 append_event call sites (joystick_intro.py: 2879,
// 2953, 2964, 3022, 3042, 3053, 3100, 3130, 3141, 3147, 3151, 3153, 3163, 3189,
// 3198, 3200, 3212, 3220, 2461). Port them in state.rs and validate in M6.
