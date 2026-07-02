//! Task configuration model.
//!
//! Mirrors every `task_config.get(...)` key read by the Python task in
//! `thalamus/task_controller/joystick_intro.py` (run() @2034, keys @2056-2113).
//! The delegate hands us `json.dumps(task_config.unwrap())` verbatim, so this
//! struct must stay in lockstep with that Python code — it is the compatibility
//! contract. When a key is added there, add it here with the SAME default.
//!
//! Parsing note: unknown keys are ignored (operators/UI add bookkeeping keys like
//! `_last_cursor_x`, `_operator_keys_pressed`, `queue_index`). Serde `default`
//! attributes reproduce the Python `.get(key, default)` fallbacks exactly.

use serde::Deserialize;

/// Lenient scalar parsing mirroring Python's cast semantics. The Qt config UI
/// stores numbers as floats (`"reward_channel": 2.0`, `"state_indicator_x":
/// 30.0`), and Python reads them through int()/float()/bool() casts — so this
/// parser must accept any JSON scalar those casts accept, or real rig configs
/// get rejected (this bit us: eevee.json crashed the first live trial).
mod lenient {
    use serde::{Deserialize, Deserializer};
    use serde_json::Value;

    fn to_f64<E: serde::de::Error>(v: &Value) -> Result<f64, E> {
        match v {
            Value::Number(n) => n
                .as_f64()
                .ok_or_else(|| E::custom("number out of f64 range")),
            Value::Bool(b) => Ok(if *b { 1.0 } else { 0.0 }),
            Value::String(s) => s
                .trim()
                .parse::<f64>()
                .map_err(|e| E::custom(format!("bad numeric string {s:?}: {e}"))),
            other => Err(E::custom(format!("expected number, got {other}"))),
        }
    }

    pub fn f64<'de, D: Deserializer<'de>>(d: D) -> Result<f64, D::Error> {
        to_f64(&Value::deserialize(d)?)
    }

    /// Python int(): truncates floats toward zero.
    pub fn i64<'de, D: Deserializer<'de>>(d: D) -> Result<i64, D::Error> {
        Ok(to_f64::<D::Error>(&Value::deserialize(d)?)? as i64)
    }

    pub fn i32<'de, D: Deserializer<'de>>(d: D) -> Result<i32, D::Error> {
        Ok(to_f64::<D::Error>(&Value::deserialize(d)?)? as i32)
    }

    /// Python bool(): number truthiness, non-empty-string truthiness.
    pub fn bool<'de, D: Deserializer<'de>>(d: D) -> Result<bool, D::Error> {
        Ok(match Value::deserialize(d)? {
            Value::Bool(b) => b,
            Value::Number(n) => n.as_f64().map(|f| f != 0.0).unwrap_or(true),
            Value::String(s) => !s.is_empty(),
            Value::Null => false,
            _ => true,
        })
    }

    /// RGB triple that may arrive as floats.
    pub fn rgb<'de, D: Deserializer<'de>>(d: D) -> Result<[u8; 3], D::Error> {
        use serde::de::Error;
        let v = Value::deserialize(d)?;
        let Value::Array(a) = &v else {
            return Err(D::Error::custom(format!("expected [r,g,b], got {v}")));
        };
        if a.len() < 3 {
            return Err(D::Error::custom("expected [r,g,b]"));
        }
        let c = |i: usize| -> Result<u8, D::Error> {
            Ok((to_f64::<D::Error>(&a[i])? as i64).clamp(0, 255) as u8)
        };
        Ok([c(0)?, c(1)?, c(2)?])
    }
}

/// Cursor integration mode (`control_mode` in the Python config).
/// Values: "direct" (default) or "cumulative". Kept as a String for 1:1 parity;
/// interpret in state.rs, matching joystick_intro.py:2886-2919.
pub type ControlMode = String;

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct TaskConfig {
    // --- input / node ---
    pub joystick_node: String,
    pub control_mode: ControlMode,
    #[serde(deserialize_with = "lenient::bool")]
    pub cursor_only_mode: bool,
    pub free_play_end_key: String,

    // --- cursor dynamics (percent -> 0..1 done at use-site, kept raw here) ---
    #[serde(deserialize_with = "lenient::f64")]
    pub up_influence_pct: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub down_influence_pct: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub left_influence_pct: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub right_influence_pct: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub cumulative_speed: f64,
    #[serde(deserialize_with = "lenient::bool")]
    pub zero_drift_mode: bool,
    #[serde(deserialize_with = "lenient::f64")]
    pub zero_drift_buffer: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub direct_range: f64,
    #[serde(deserialize_with = "lenient::bool")]
    pub direct_recenter_when_idle: bool,

    // --- cursor appearance / reset ---
    #[serde(deserialize_with = "lenient::f64")]
    pub cursor_diameter_ratio: f64,
    #[serde(deserialize_with = "lenient::rgb")]
    pub cursor_color: [u8; 3],
    #[serde(deserialize_with = "lenient::bool")]
    pub reset_cursor_each_trial: bool,
    #[serde(deserialize_with = "lenient::bool")]
    pub require_center_before_trial: bool,
    #[serde(deserialize_with = "lenient::f64")]
    pub center_gate_radius_ratio: f64,

    // --- task region (normalized 0..1 of the subject canvas) ---
    #[serde(deserialize_with = "lenient::f64")]
    pub task_region_x: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub task_region_y: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub task_region_width: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub task_region_height: f64,

    // --- reward ---
    #[serde(deserialize_with = "lenient::i32")]
    pub reward_channel: i32,
    #[serde(deserialize_with = "lenient::f64")]
    pub reward_scale: f64,

    // --- free-play rewards ---
    #[serde(deserialize_with = "lenient::f64")]
    pub free_play_active_threshold: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub free_play_reward_threshold: f64,
    #[serde(deserialize_with = "lenient::bool")]
    pub free_play_first_touch_reward_enabled: bool,
    #[serde(deserialize_with = "lenient::i32")]
    pub free_play_first_touch_reward_channel: i32,
    #[serde(deserialize_with = "lenient::bool")]
    pub free_play_bout_reward_enabled: bool,
    #[serde(deserialize_with = "lenient::i32")]
    pub free_play_bout_reward_channel: i32,
    #[serde(deserialize_with = "lenient::f64")]
    pub free_play_bout_cooldown_s: f64,
    #[serde(deserialize_with = "lenient::bool")]
    pub free_play_sustain_reward_enabled: bool,
    #[serde(deserialize_with = "lenient::i32")]
    pub free_play_sustain_reward_channel: i32,
    #[serde(deserialize_with = "lenient::f64")]
    pub free_play_sustain_initial_delay_s: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub free_play_sustain_interval_s: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub free_play_reward_cooldown_s: f64,

    // --- trial timing / outcomes ---
    #[serde(deserialize_with = "lenient::f64")]
    pub trial_timeout: f64,
    #[serde(deserialize_with = "lenient::f64")]
    pub intertrial_interval: f64,
    #[serde(deserialize_with = "lenient::bool")]
    pub ignore_idle_trial_failures: bool,
    #[serde(deserialize_with = "lenient::i64")]
    pub ignored_idle_sample_clear_threshold: i64,
    #[serde(deserialize_with = "lenient::i64")]
    pub max_logged_joystick_samples: i64,
    #[serde(deserialize_with = "lenient::bool")]
    pub fail_on_touch_input: bool,

    // --- streak / HUD / animations (display-only; still parsed for parity) ---
    #[serde(deserialize_with = "lenient::bool")]
    pub animations_enabled: bool,
    #[serde(deserialize_with = "lenient::bool")]
    pub task_animation_enabled: bool,
    #[serde(deserialize_with = "lenient::bool")]
    pub target_animation_enabled: bool,
    #[serde(deserialize_with = "lenient::bool")]
    pub show_streak_hud: bool,
    #[serde(deserialize_with = "lenient::i64")]
    pub streak_bonus_threshold: i64,
    #[serde(deserialize_with = "lenient::i64")]
    pub streak_bonus_reward_count: i64,
    #[serde(deserialize_with = "lenient::bool")]
    pub streak_reset_on_bonus: bool,
    #[serde(deserialize_with = "lenient::bool")]
    pub show_hold_progress_ring: bool,
    #[serde(deserialize_with = "lenient::bool")]
    pub show_success_pop: bool,
    #[serde(deserialize_with = "lenient::bool")]
    pub show_success_particles: bool,
    #[serde(deserialize_with = "lenient::f64")]
    pub success_pop_duration_s: f64,
    #[serde(deserialize_with = "lenient::i64")]
    pub state_indicator_x: i64,
    #[serde(deserialize_with = "lenient::i64")]
    pub state_indicator_y: i64,

    // --- targets ---
    // Operator-defined target list. Schema is rich and still evolving in the
    // Python UI; kept as raw JSON for now. TODO(M3): promote to a typed `Target`
    // once the state machine port pins down which fields it reads.
    pub targets: Vec<serde_json::Value>,
}

impl Default for TaskConfig {
    fn default() -> Self {
        // Defaults MUST match joystick_intro.py:2056-2113 exactly.
        Self {
            joystick_node: "Joystick".to_string(),
            control_mode: "direct".to_string(),
            cursor_only_mode: false,
            free_play_end_key: "space".to_string(),

            up_influence_pct: 100.0,
            down_influence_pct: 100.0,
            left_influence_pct: 100.0,
            right_influence_pct: 100.0,
            cumulative_speed: 0.70,
            zero_drift_mode: true,
            zero_drift_buffer: 0.03,
            direct_range: 0.45,
            direct_recenter_when_idle: true,

            cursor_diameter_ratio: 0.1,
            cursor_color: [255, 70, 70],
            reset_cursor_each_trial: true,
            require_center_before_trial: false,
            center_gate_radius_ratio: 0.15,

            task_region_x: 0.5,
            task_region_y: 0.270,
            task_region_width: 0.5,
            task_region_height: 0.67,

            reward_channel: 0,
            reward_scale: 1.0,

            free_play_active_threshold: 0.0,
            free_play_reward_threshold: 0.0,
            free_play_first_touch_reward_enabled: false,
            free_play_first_touch_reward_channel: 0,
            free_play_bout_reward_enabled: false,
            free_play_bout_reward_channel: 0,
            free_play_bout_cooldown_s: 1.0,
            free_play_sustain_reward_enabled: false,
            free_play_sustain_reward_channel: 0,
            free_play_sustain_initial_delay_s: 0.0,
            free_play_sustain_interval_s: 1.0,
            free_play_reward_cooldown_s: 1.0,

            trial_timeout: 0.5,
            intertrial_interval: 1.0,
            ignore_idle_trial_failures: false,
            ignored_idle_sample_clear_threshold: 50,
            max_logged_joystick_samples: 2000, // DEFAULT_MAX_LOGGED_JOYSTICK_SAMPLES (joystick_intro.py:49)
            fail_on_touch_input: false,

            animations_enabled: false,
            task_animation_enabled: true,
            target_animation_enabled: true,
            show_streak_hud: true,
            streak_bonus_threshold: 0,
            streak_bonus_reward_count: 1,
            streak_reset_on_bonus: false,
            show_hold_progress_ring: true,
            show_success_pop: true,
            show_success_particles: true,
            success_pop_duration_s: 0.12,
            state_indicator_x: 30,
            state_indicator_y: 70,

            targets: Vec::new(),
        }
    }
}

impl TaskConfig {
    /// Parse the delegate-provided `config_json` (json.dumps of the task_config).
    pub fn from_json(config_json: &str) -> anyhow::Result<Self> {
        Ok(serde_json::from_str(config_json)?)
    }

    /// Resolved 0..1 direction-influence factors (Python clamps pct/100 to [0,1]).
    pub fn influence(&self) -> Influence {
        let c = |p: f64| (p / 100.0).clamp(0.0, 1.0);
        Influence {
            up: c(self.up_influence_pct),
            down: c(self.down_influence_pct),
            left: c(self.left_influence_pct),
            right: c(self.right_influence_pct),
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct Influence {
    pub up: f64,
    pub down: f64,
    pub left: f64,
    pub right: f64,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_json_yields_python_defaults() {
        let c = TaskConfig::from_json("{}").unwrap();
        assert_eq!(c.joystick_node, "Joystick");
        assert_eq!(c.control_mode, "direct");
        assert_eq!(c.trial_timeout, 0.5);
        assert_eq!(c.cursor_color, [255, 70, 70]);
        assert_eq!(c.task_region_y, 0.270);
    }

    #[test]
    fn unknown_keys_are_ignored() {
        // Bookkeeping keys the operator UI injects must not break parsing.
        let json = r#"{"_last_cursor_x": 0.3, "queue_index": 4, "joystick_node": "Joy2"}"#;
        let c = TaskConfig::from_json(json).unwrap();
        assert_eq!(c.joystick_node, "Joy2");
    }

    #[test]
    fn accepts_qt_float_typed_config() {
        // Regression: real rig configs (eevee.json) store int-like values as
        // floats — the Qt Form UI writes 2.0/30.0 — and Python reads them
        // through int() casts. The first live trial crashed on this.
        let json = r#"{
            "task_type": "joystick_intro", "goal": 50,
            "reward_channel": 2.0, "trial_timeout": 1.0,
            "intertrial_interval": 1.0,
            "state_indicator_x": 30.0, "state_indicator_y": 70.0,
            "free_play_reward_threshold": 0.0, "free_play_reward_cooldown_s": 1.0,
            "free_play_bout_cooldown_s": 1.0, "free_play_sustain_initial_delay_s": 0.0,
            "free_play_sustain_interval_s": 1.0, "reward_scale": 1.0,
            "streak_bonus_threshold": 5.0, "max_logged_joystick_samples": 2000.0,
            "cursor_color": [255.0, 70.0, 70.0],
            "cursor_only_mode": 0,
            "targets": [{"enabled": true, "x_norm": 0.65, "reward_channel": 2}]
        }"#;
        let c = TaskConfig::from_json(json).unwrap();
        assert_eq!(c.reward_channel, 2);
        assert_eq!(c.state_indicator_x, 30);
        assert_eq!(c.state_indicator_y, 70);
        assert_eq!(c.streak_bonus_threshold, 5);
        assert_eq!(c.max_logged_joystick_samples, 2000);
        assert_eq!(c.cursor_color, [255, 70, 70]);
        assert!(!c.cursor_only_mode);
        assert_eq!(c.trial_timeout, 1.0);
    }

    #[test]
    fn influence_clamps_like_python() {
        let json = r#"{"up_influence_pct": 150, "down_influence_pct": -20}"#;
        let c = TaskConfig::from_json(json).unwrap();
        let inf = c.influence();
        assert_eq!(inf.up, 1.0);
        assert_eq!(inf.down, 0.0);
    }
}
