//! Behavioral event / `behav_result` model.
//!
//! Byte-compatibility target: after `json.loads`/`json.dumps` normalization, the
//! `behav_result` this produces must be identical to the Python one so downstream
//! analysis (record_reader) is unaffected. Rules:
//!   1. KEY ORDER must match Python's dict insertion order. serde serializes
//!      struct fields in declaration order, so field order below mirrors Python
//!      exactly (initial dict literal first, later insertions after).
//!   2. Fields Python initializes to None emit `null` (Option WITHOUT skip).
//!   3. Keys Python only inserts in some modes (the free-play block, the
//!      finalize_attempt tail, final_attempt) are ABSENT until inserted, so
//!      those use skip_serializing_if.
//!
//! Sources (joystick_intro.py):
//!   - behav_result literal        @2184-2202
//!   - reset_attempt_tracking      @2373-2407
//!   - free-play attempt extras    @2860-2878
//!   - append_event                @2409-2418
//!   - finalize_attempt            @2420-2437
//!   - joystick sample bookkeeping @2279-2297
//!   - idle sample clears          @2439-2456

use indexmap::IndexMap;
use serde::Serialize;
use serde_json::Value;

/// One behavioral event within an attempt.
/// `name`, `time_perf_counter`, `time_since_attempt_start_s` come first (Python
/// builds them in that order), then per-call-site extras via `extra`.
#[derive(Debug, Clone, Serialize)]
pub struct Event {
    pub name: String,
    pub time_perf_counter: f64,
    pub time_since_attempt_start_s: f64,
    /// Per-call-site keyword extras (append_event(**extra)). Order-preserving.
    #[serde(flatten)]
    pub extra: IndexMap<String, Value>,
}

impl Event {
    /// append_event (joystick_intro.py:2409-2418): time_since_attempt_start_s
    /// is clamped at 0 against the attempt start.
    pub fn new(name: &str, time_s: f64, attempt_start_s: f64) -> Self {
        Self {
            name: name.to_string(),
            time_perf_counter: time_s,
            time_since_attempt_start_s: (time_s - attempt_start_s).max(0.0),
            extra: IndexMap::new(),
        }
    }

    pub fn with(mut self, key: &str, value: impl Into<Value>) -> Self {
        self.extra.insert(key.to_string(), value.into());
        self
    }
}

/// Python: {"x_norm": target_x, "y_norm": target_y} (joystick_intro.py:3084).
#[derive(Debug, Clone, Serialize)]
pub struct TargetPosition {
    pub x_norm: f64,
    pub y_norm: f64,
}

/// Free-play bookkeeping keys inserted into the attempt dict only in
/// cursor_only_mode (joystick_intro.py:2860-2878), in this exact order.
#[derive(Debug, Clone, Serialize)]
pub struct FreePlayAttempt {
    pub free_play_active_threshold: f64,
    pub free_play_first_touch_reward_enabled: bool,
    pub free_play_first_touch_reward_channel: i64,
    pub free_play_bout_reward_enabled: bool,
    pub free_play_bout_reward_channel: i64,
    pub free_play_bout_cooldown_s: f64,
    pub free_play_sustain_reward_enabled: bool,
    pub free_play_sustain_reward_channel: i64,
    pub free_play_sustain_initial_delay_s: f64,
    pub free_play_sustain_interval_s: f64,
    pub free_play_first_touch_reward_count: i64,
    pub free_play_bout_reward_count: i64,
    pub free_play_sustain_reward_count: i64,
    pub free_play_total_reward_count: i64,
    pub free_play_active_bout_count: i64,
    pub free_play_total_active_time_s: f64,
}

/// Terminal fields appended by finalize_attempt (joystick_intro.py:2427-2432).
/// Python inserts these keys only at finalization, so they are absent before.
#[derive(Debug, Clone, Serialize)]
pub struct AttemptEnd {
    pub end_time_perf_counter: f64,
    pub duration_s: f64,
    pub first_movement_time_s: Option<f64>,
    pub first_target_entry_time_s: Option<f64>,
    pub first_hold_start_time_s: Option<f64>,
    pub success_time_s: Option<f64>,
}

/// One trial attempt. Field order matches the Python dict insertion order.
#[derive(Debug, Clone, Serialize)]
pub struct Attempt {
    pub attempt_index: i64,
    pub start_time_perf_counter: f64,
    pub control_mode: String,
    pub cursor_only_mode: bool,
    pub target_index: Option<i64>,
    pub target_position: Option<TargetPosition>,
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
    /// cursor_only_mode only (keys absent otherwise).
    #[serde(flatten, skip_serializing_if = "Option::is_none")]
    pub free_play: Option<FreePlayAttempt>,
    /// Present only after finalize_attempt.
    #[serde(flatten, skip_serializing_if = "Option::is_none")]
    pub end: Option<AttemptEnd>,
}

impl Attempt {
    /// reset_attempt_tracking's dict literal (joystick_intro.py:2388-2407).
    pub fn new(attempt_index: i64, start_time_s: f64, control_mode: &str, cursor_only_mode: bool) -> Self {
        Self {
            attempt_index,
            start_time_perf_counter: start_time_s,
            control_mode: control_mode.to_string(),
            cursor_only_mode,
            target_index: None,
            target_position: None,
            target_radius_ratio: None,
            hold_time_s: None,
            reward_channel: None,
            target_color_rgb: None,
            target_opacity: None,
            target_active_color_rgb: None,
            target_active_opacity: None,
            events: Vec::new(),
            joystick_active: false,
            target_entry_count: 0,
            outcome: None,
            failure_reason: None,
            free_play: None,
            end: None,
        }
    }
}

/// joystick_samples entries (append_joystick_sample @2287-2292).
#[derive(Debug, Clone, Serialize)]
pub struct JoystickSample {
    pub time_perf_counter: f64,
    pub time_since_session_start_s: f64,
    pub x: f64,
    pub y: f64,
}

/// ignored_idle_sample_clear_events entries (@2447-2452).
#[derive(Debug, Clone, Serialize)]
pub struct SampleClearEvent {
    pub time_perf_counter: f64,
    pub time_since_session_start_s: f64,
    pub ignored_idle_trial_count: i64,
    pub cleared_sample_count: i64,
}

/// Top-level behav_result — field order matches the Python literal @2184-2202,
/// with `final_attempt` appended at the end on first finalize (@2436).
#[derive(Debug, Clone, Serialize)]
pub struct BehavResult {
    pub task: String, // always "joystick_intro"
    pub control_mode: String,
    pub cursor_only_mode: bool,
    pub require_center_before_trial: bool,
    pub center_gate_radius_ratio: f64,
    pub ignored_idle_sample_clear_threshold: i64,
    pub max_logged_joystick_samples: i64,
    pub ignored_idle_sample_clear_events: Vec<SampleClearEvent>,
    pub fail_on_touch_input: bool,
    pub trial_attempt_count: i64,
    pub attempts: Vec<Attempt>,
    pub joystick_samples: Vec<JoystickSample>,
    pub joystick_sample_count: i64,
    pub joystick_samples_dropped: i64,
    pub joystick_samples_kept: i64,
    pub session_start_perf_counter: f64,
    pub final_outcome: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub final_attempt: Option<Attempt>,
}

impl BehavResult {
    pub fn new(
        control_mode: &str,
        cursor_only_mode: bool,
        require_center_before_trial: bool,
        center_gate_radius_ratio: f64,
        ignored_idle_sample_clear_threshold: i64,
        max_logged_joystick_samples: i64,
        fail_on_touch_input: bool,
        session_start_s: f64,
    ) -> Self {
        Self {
            task: "joystick_intro".to_string(),
            control_mode: control_mode.to_string(),
            cursor_only_mode,
            require_center_before_trial,
            center_gate_radius_ratio,
            ignored_idle_sample_clear_threshold,
            max_logged_joystick_samples,
            ignored_idle_sample_clear_events: Vec::new(),
            fail_on_touch_input,
            trial_attempt_count: 0,
            attempts: Vec::new(),
            joystick_samples: Vec::new(),
            joystick_sample_count: 0,
            joystick_samples_dropped: 0,
            joystick_samples_kept: 0,
            session_start_perf_counter: session_start_s,
            final_outcome: None,
            final_attempt: None,
        }
    }

    /// append_joystick_sample (joystick_intro.py:2279-2297).
    pub fn append_joystick_sample(&mut self, time_s: f64, x: f64, y: f64) {
        self.joystick_sample_count += 1;
        if self.max_logged_joystick_samples <= 0 {
            self.joystick_samples_dropped += 1;
            self.joystick_samples_kept = 0;
            return;
        }
        self.joystick_samples.push(JoystickSample {
            time_perf_counter: time_s,
            time_since_session_start_s: (time_s - self.session_start_perf_counter).max(0.0),
            x,
            y,
        });
        let max = self.max_logged_joystick_samples as usize;
        if self.joystick_samples.len() > max {
            let overflow = self.joystick_samples.len() - max;
            self.joystick_samples.drain(..overflow);
            self.joystick_samples_dropped += overflow as i64;
        }
        self.joystick_samples_kept = self.joystick_samples.len() as i64;
    }

    /// clear_joystick_samples_for_prolonged_idle (joystick_intro.py:2439-2456).
    pub fn clear_samples_for_idle(&mut self, now_s: f64, ignored_idle_trial_count: i64) {
        let cleared = self.joystick_samples.len() as i64;
        if cleared <= 0 {
            return;
        }
        self.ignored_idle_sample_clear_events.push(SampleClearEvent {
            time_perf_counter: now_s,
            time_since_session_start_s: (now_s - self.session_start_perf_counter).max(0.0),
            ignored_idle_trial_count,
            cleared_sample_count: cleared,
        });
        self.joystick_samples.clear();
        self.joystick_samples_dropped += cleared;
        self.joystick_samples_kept = 0;
    }

    /// finalize_attempt's summary updates (joystick_intro.py:2433-2437).
    pub fn push_attempt(&mut self, attempt: Attempt) {
        let outcome = attempt.outcome.clone();
        self.attempts.push(attempt.clone());
        self.trial_attempt_count = self.attempts.len() as i64;
        self.final_outcome = outcome;
        self.final_attempt = Some(attempt);
    }

    /// Serialize for TrialEvent.behav_result_json. The delegate hands this to
    /// Python which sets context.behav_result unchanged.
    pub fn to_json(&self) -> anyhow::Result<String> {
        Ok(serde_json::to_string(self)?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn behav_result_key_order_matches_python_literal() {
        let b = BehavResult::new("direct", false, false, 0.15, 50, 2000, false, 100.0);
        let json = b.to_json().unwrap();
        let keys: Vec<&str> = json
            .split('"')
            .skip(1)
            .step_by(2)
            .take_while(|_| true)
            .collect();
        // The top-level keys, in Python insertion order (final_attempt absent
        // until a finalize). Flattened check: the first N quoted strings are the
        // top-level keys because no nested objects/strings precede them except
        // "joystick_intro" (value of task) and "direct" (value of control_mode).
        assert!(json.starts_with(
            r#"{"task":"joystick_intro","control_mode":"direct","cursor_only_mode":false,"require_center_before_trial":false,"center_gate_radius_ratio":0.15,"ignored_idle_sample_clear_threshold":50,"max_logged_joystick_samples":2000,"ignored_idle_sample_clear_events":[],"fail_on_touch_input":false,"trial_attempt_count":0,"attempts":[],"joystick_samples":[],"joystick_sample_count":0,"joystick_samples_dropped":0,"joystick_samples_kept":0,"session_start_perf_counter":100.0,"final_outcome":null}"#
        ), "unexpected serialization: {json} (keys: {keys:?})");
    }

    #[test]
    fn attempt_initial_keys_match_reset_attempt_tracking() {
        let a = Attempt::new(1, 10.0, "direct", false);
        let json = serde_json::to_string(&a).unwrap();
        assert!(json.starts_with(
            r#"{"attempt_index":1,"start_time_perf_counter":10.0,"control_mode":"direct","cursor_only_mode":false,"target_index":null,"target_position":null,"target_radius_ratio":null,"hold_time_s":null,"reward_channel":null,"target_color_rgb":null,"target_opacity":null,"target_active_color_rgb":null,"target_active_opacity":null,"events":[],"joystick_active":false,"target_entry_count":0,"outcome":null,"failure_reason":null}"#
        ), "unexpected: {json}");
    }

    #[test]
    fn sample_cap_and_drop_bookkeeping() {
        let mut b = BehavResult::new("direct", false, false, 0.15, 50, 3, false, 0.0);
        for i in 0..5 {
            b.append_joystick_sample(i as f64, 0.1, 0.2);
        }
        assert_eq!(b.joystick_sample_count, 5);
        assert_eq!(b.joystick_samples_kept, 3);
        assert_eq!(b.joystick_samples_dropped, 2);
        assert_eq!(b.joystick_samples[0].time_perf_counter, 2.0);
        // max_logged <= 0 drops everything.
        let mut b0 = BehavResult::new("direct", false, false, 0.15, 50, 0, false, 0.0);
        b0.append_joystick_sample(1.0, 0.0, 0.0);
        assert_eq!(b0.joystick_sample_count, 1);
        assert_eq!(b0.joystick_samples_dropped, 1);
        assert_eq!(b0.joystick_samples_kept, 0);
    }

    #[test]
    fn event_extras_preserve_order() {
        let e = Event::new("target_on", 5.0, 4.0)
            .with("target_index", 2)
            .with("target_x", 0.75)
            .with("target_y", 0.5);
        let json = serde_json::to_string(&e).unwrap();
        assert_eq!(
            json,
            r#"{"name":"target_on","time_perf_counter":5.0,"time_since_attempt_start_s":1.0,"target_index":2,"target_x":0.75,"target_y":0.5}"#
        );
    }
}
