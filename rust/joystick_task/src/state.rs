//! Joystick_intro state machine — faithful port of the Python run() main loop
//! (joystick_intro.py:2034-3246).
//!
//! The trial is a pure-ish state machine: `step()` consumes one frame's inputs
//! (latest joystick sample, operator override keys, touch, timestamps) and
//! returns side effects (BehavState logs, reward pulses) plus the terminal
//! outcome. All I/O (gRPC log/reward, rendering) happens OUTSIDE this module so
//! the machine is unit-testable against the Python semantics.
//!
//! The loop runs at the DISPLAY frame rate (240 Hz) driven by render::run_loop,
//! NOT the Python 100 Hz sleep — that is the whole point of the patch. dt is
//! computed exactly like Python (clamped 0..0.05 s), so integration dynamics
//! match; only the tick density differs.
//!
//! Python-parity notes:
//!   - Every timestamp here is in the Python perf_counter domain, SECONDS
//!     (behav_result floats). Map via clock.rs before calling.
//!   - Pixel math replicates Python's int() truncation (as i64 casts).
//!   - The success-pop/particle stall (@3167-3188, animations only) is NOT
//!     ported: finalization is immediate. Defaults have animations disabled.

use serde_json::Value;

use crate::config::{Influence, TaskConfig};
use crate::constants::*;
use crate::events::{
    Attempt, AttemptEnd, BehavResult, Event, FreePlayAttempt, TargetPosition,
};

/// Behavioral state (mirrors the Python string values for log/marker parity).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    Intertrial,
    StartOn,
}

/// Side effects the caller must perform, in order.
#[derive(Debug, Clone, PartialEq)]
pub enum Effect {
    /// Log "BehavState=<str>" to Thalamus AND stream a TrialEvent marker.
    LogState(&'static str),
    /// Deliver `repeats` reward pulses on `channel` (50 ms apart, Python
    /// deliver_reward_repeats @2367-2371).
    Reward { channel: i64, repeats: i64 },
}

#[derive(Debug, Clone, Copy)]
pub struct TrialOutcome {
    pub success: bool,
}

/// One frame's inputs.
#[derive(Debug, Clone, Copy)]
pub struct StepInput {
    /// Python perf_counter domain, seconds.
    pub now_s: f64,
    /// Latest analog joystick sample.
    pub analog_x: f64,
    pub analog_y: f64,
    /// Subject surface size in pixels.
    pub width_px: i64,
    pub height_px: i64,
}

#[derive(Debug, Default)]
pub struct StepOutput {
    pub effects: Vec<Effect>,
    pub done: Option<TrialOutcome>,
}

impl StepOutput {
    fn log(&mut self, s: &'static str) {
        self.effects.push(Effect::LogState(s));
    }
}

/// clamp_float (joystick_intro.py:54-59): float() accepts numbers, bools and
/// numeric strings; anything else -> default. Then clamp.
pub fn clamp_float(v: Option<&Value>, lo: f64, hi: f64, default: f64) -> f64 {
    let value = match v {
        Some(Value::Number(n)) => n.as_f64().unwrap_or(default),
        Some(Value::Bool(b)) => {
            if *b {
                1.0
            } else {
                0.0
            }
        }
        Some(Value::String(s)) => s.trim().parse::<f64>().unwrap_or(default),
        _ => default,
    };
    value.clamp(lo, hi)
}

/// normalize_rgb (joystick_intro.py:61-73).
pub fn normalize_rgb(v: Option<&Value>, default: [i64; 3]) -> [i64; 3] {
    if let Some(Value::Array(a)) = v {
        if a.len() >= 3 {
            let comp = |x: &Value| -> Option<i64> {
                match x {
                    Value::Number(n) => n.as_f64().map(|f| f as i64),
                    Value::Bool(b) => Some(*b as i64),
                    Value::String(s) => s.trim().parse::<f64>().ok().map(|f| f as i64),
                    _ => None,
                }
            };
            if let (Some(r), Some(g), Some(b)) = (comp(&a[0]), comp(&a[1]), comp(&a[2])) {
                return [r.clamp(0, 255), g.clamp(0, 255), b.clamp(0, 255)];
            }
        }
    }
    default
}

/// toggle_brightness (joystick_intro.py:51-52).
fn toggle_brightness(b: i64) -> i64 {
    if b == 255 {
        0
    } else {
        255
    }
}

/// A selected target (Python TargetSelection tuple @2138, plus the schedule
/// extras the Rust executor adds: the target's `name` so structured schedules
/// can reference it, and the `schedule_phase` tag propagated into behav_result).
#[derive(Debug, Clone)]
pub struct TargetSelection {
    pub index: i64,
    pub x: f64,
    pub y: f64,
    pub radius_ratio: f64,
    pub hold_time: f64,
    pub reward_channel: i64,
    pub color: [i64; 3],
    pub opacity: f64,
    pub active_color: [i64; 3],
    pub active_opacity: f64,
    pub name: String,
    /// "random" | "sequence" | "center" | "peripheral"
    pub schedule_phase: String,
}

/// Cheap xorshift64* RNG for place_target's random.choice — no rand dep.
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Self {
        Self(seed | 1)
    }
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545F4914F6CDD1D)
    }
    fn choice(&mut self, len: usize) -> usize {
        (self.next() % len as u64) as usize
    }
    /// Uniform float in [0, 1).
    fn next_f64(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }
}

/// Structured-schedule cursor. A fresh Trial is built per run_trial stream, so
/// this state round-trips through task_config like _streak_count does: parsed
/// from the `_schedule_*` keys at construction, shipped back via
/// config_updates() at trial end. The layout editor resets the keys on save.
#[derive(Debug, Clone, Copy)]
struct SchedulerState {
    seq_pos: u64,
    /// true => the next structured center_out pick is the center target.
    expect_center: bool,
    peripheral_pos: u64,
}

/// Operator keyboard override state (arrow keys on the operator widget).
#[derive(Debug, Clone, Copy, Default)]
pub struct OperatorKeys {
    pub left: bool,
    pub right: bool,
    pub up: bool,
    pub down: bool,
}

/// Cross-trial bookkeeping Python persists inside task_config.
#[derive(Debug, Clone, Copy)]
struct Persisted {
    streak_count: i64,
    last_cursor_x: f64,
    last_cursor_y: f64,
    keys: OperatorKeys,
    scheduler: SchedulerState,
}

fn parse_persisted(raw: &Value) -> Persisted {
    let f = |k: &str, d: f64| raw.get(k).and_then(Value::as_f64).unwrap_or(d);
    let keys = raw
        .get("_operator_keys_pressed")
        .and_then(Value::as_object);
    let key = |k: &str| {
        keys.and_then(|m| m.get(k))
            .and_then(Value::as_bool)
            .unwrap_or(false)
    };
    Persisted {
        // as_f64 first: the Qt UI stores counters as floats (3.0).
        streak_count: (raw
            .get("_streak_count")
            .and_then(Value::as_f64)
            .unwrap_or(0.0) as i64)
            .max(0),
        last_cursor_x: f("_last_cursor_x", 0.5),
        last_cursor_y: f("_last_cursor_y", 0.5),
        keys: OperatorKeys {
            left: key("left"),
            right: key("right"),
            up: key("up"),
            down: key("down"),
        },
        scheduler: SchedulerState {
            seq_pos: f("_schedule_seq_pos", 0.0).max(0.0) as u64,
            expect_center: raw
                .get("_schedule_expect_center")
                .and_then(Value::as_bool)
                .unwrap_or(true),
            peripheral_pos: f("_schedule_peripheral_pos", 0.0).max(0.0) as u64,
        },
    }
}

/// Per-trial state machine. Field names follow the Python locals.
pub struct Trial {
    pub cfg: TaskConfig,
    influence: Influence,
    rng: Rng,

    // Resolved config (clamped like Python @2205-2208, @2179-2183).
    region_x: f64,
    region_y: f64,
    region_w: f64,
    region_h: f64,
    free_play_active_threshold: f64,
    free_play_sustain_interval_s: f64,
    task_animation_enabled: bool,

    // Cross-trial persisted.
    pub streak_count: i64,
    pub operator_keys: OperatorKeys,
    scheduler: SchedulerState,

    // Dynamic state.
    pub session_start: f64,
    pub state: State,
    pub cursor_x: f64,
    pub cursor_y: f64,
    pub target_x: f64,
    pub target_y: f64,
    pub state_brightness: i64,
    pub hold_progress_ratio: f64,
    pub cursor_inside_target: bool,

    // Success-pop visual hold. Set at hold-complete when the pop is enabled;
    // while Some, step() short-circuits and keeps the trial rendering (target +
    // expanding pop) until success_pop_duration_s elapses, then finishes the
    // trial. success_pop_ratio is the 0..1 pop progress read by the renderer.
    pub success_pop_start: Option<f64>,
    pub success_pop_ratio: f64,
    pub success_pop_x: f64,
    pub success_pop_y: f64,

    hold_start: Option<f64>,
    trial_start: f64,
    last_tick: f64,
    iti_end: f64,
    intertrial_center_ready: bool,
    pub current_target: TargetSelection,
    next_target_preview: Option<TargetSelection>,
    free_play_end_requested: bool,
    operator_cursor_latched: bool,
    joystick_active_this_trial: bool,
    touch_detected_this_trial: bool,
    last_touch_pos: (i64, i64),
    last_touch_time: Option<f64>,
    trial_index: i64,
    ignored_idle_trial_count: i64,
    target_entry_count: i64,
    first_movement_time: Option<f64>,
    first_target_entry_time: Option<f64>,
    first_hold_start_time: Option<f64>,
    previous_cursor_inside_target: bool,
    current_attempt: Option<Attempt>,

    // Free-play bookkeeping.
    free_play_was_active: bool,
    free_play_active_bout_start: Option<f64>,
    free_play_first_touch_delivered: bool,
    free_play_last_bout_reward_time: Option<f64>,
    free_play_last_sustain_reward_time: Option<f64>,
    free_play_first_touch_reward_count: i64,
    free_play_bout_reward_count: i64,
    free_play_sustain_reward_count: i64,
    free_play_total_reward_count: i64,
    free_play_active_bout_count: i64,
    free_play_total_active_time_s: f64,

    pub behav: BehavResult,
}

impl Trial {
    /// `raw_config` is the same JSON the TaskConfig was parsed from (for the
    /// `_`-prefixed persisted keys). `now_s` is the Python-domain trial start.
    pub fn new(cfg: TaskConfig, raw_config: &Value, now_s: f64, rng_seed: u64) -> Self {
        let persisted = parse_persisted(raw_config);
        let reset = cfg.reset_cursor_each_trial;

        // @2079: free_play_active_threshold falls back to the LEGACY key
        // free_play_reward_threshold when absent — a chain TaskConfig defaults
        // cannot express, so read it from the raw JSON.
        let free_play_active_config_threshold = clamp_float(
            raw_config
                .get("free_play_active_threshold")
                .or_else(|| raw_config.get("free_play_reward_threshold")),
            0.0,
            1.0,
            0.0,
        );
        // @2179-2183: zero threshold falls back to the motion threshold.
        let free_play_active_threshold = if free_play_active_config_threshold > 0.0 {
            free_play_active_config_threshold
        } else if cfg.zero_drift_mode {
            cfg.zero_drift_buffer
        } else {
            0.02
        };
        // @2088: same legacy-key chain for the sustain interval.
        let free_play_sustain_interval_s = clamp_float(
            raw_config
                .get("free_play_sustain_interval_s")
                .or_else(|| raw_config.get("free_play_reward_cooldown_s")),
            0.001,
            f64::INFINITY,
            1.0,
        );

        let behav = BehavResult::new(
            &cfg.control_mode,
            cfg.cursor_only_mode,
            cfg.require_center_before_trial,
            cfg.center_gate_radius_ratio.clamp(0.001, 1.0),
            cfg.ignored_idle_sample_clear_threshold.max(0),
            cfg.max_logged_joystick_samples.max(0),
            cfg.fail_on_touch_input,
            now_s,
        );

        let (cursor_x, cursor_y) = if reset {
            (0.5, 0.5)
        } else {
            (
                persisted.last_cursor_x.clamp(0.0, 1.0),
                persisted.last_cursor_y.clamp(0.0, 1.0),
            )
        };

        let default_target = TargetSelection {
            index: -1,
            x: 0.75,
            y: 0.50,
            radius_ratio: DEFAULT_TARGET_RADIUS_RATIO,
            hold_time: DEFAULT_TARGET_HOLD_TIME,
            reward_channel: cfg.reward_channel as i64,
            color: DEFAULT_TARGET_COLOR_I,
            opacity: DEFAULT_TARGET_OPACITY,
            active_color: DEFAULT_TARGET_ACTIVE_COLOR_I,
            active_opacity: DEFAULT_TARGET_ACTIVE_OPACITY,
            name: String::new(),
            schedule_phase: "random".to_string(),
        };

        let mut t = Self {
            influence: cfg.influence(),
            region_x: cfg.task_region_x.clamp(0.0, 1.0),
            region_y: cfg.task_region_y.clamp(0.0, 1.0),
            region_w: cfg.task_region_width.clamp(0.05, 1.0),
            region_h: cfg.task_region_height.clamp(0.05, 1.0),
            free_play_active_threshold,
            free_play_sustain_interval_s,
            task_animation_enabled: cfg.animations_enabled && cfg.task_animation_enabled,
            streak_count: persisted.streak_count,
            operator_keys: if reset {
                OperatorKeys::default()
            } else {
                persisted.keys
            },
            scheduler: persisted.scheduler,
            session_start: now_s,
            state: State::Intertrial,
            cursor_x,
            cursor_y,
            target_x: 0.5,
            target_y: 0.5,
            state_brightness: 0,
            hold_progress_ratio: 0.0,
            cursor_inside_target: false,
            success_pop_start: None,
            success_pop_ratio: 0.0,
            success_pop_x: 0.5,
            success_pop_y: 0.5,
            hold_start: None,
            trial_start: now_s,
            last_tick: now_s,
            // @2128-2129
            iti_end: if cfg.require_center_before_trial {
                f64::INFINITY
            } else {
                now_s + cfg.intertrial_interval
            },
            intertrial_center_ready: false,
            current_target: default_target,
            next_target_preview: None,
            free_play_end_requested: false,
            operator_cursor_latched: false,
            joystick_active_this_trial: false,
            touch_detected_this_trial: false,
            last_touch_pos: (0, 0),
            last_touch_time: None,
            trial_index: 0,
            ignored_idle_trial_count: 0,
            target_entry_count: 0,
            first_movement_time: None,
            first_target_entry_time: None,
            first_hold_start_time: None,
            previous_cursor_inside_target: false,
            current_attempt: None,
            free_play_was_active: false,
            free_play_active_bout_start: None,
            free_play_first_touch_delivered: false,
            free_play_last_bout_reward_time: None,
            free_play_last_sustain_reward_time: None,
            free_play_first_touch_reward_count: 0,
            free_play_bout_reward_count: 0,
            free_play_sustain_reward_count: 0,
            free_play_total_reward_count: 0,
            free_play_active_bout_count: 0,
            free_play_total_active_time_s: 0.0,
            behav,
            rng: Rng::new(rng_seed),
            cfg,
        };

        // Pre-trial setup (@2857-2879).
        if !t.cfg.cursor_only_mode {
            t.ensure_next_target_preview();
        } else {
            t.reset_attempt_tracking(now_s);
            if let Some(a) = t.current_attempt.as_mut() {
                a.free_play = Some(FreePlayAttempt {
                    free_play_active_threshold: t.free_play_active_threshold,
                    free_play_first_touch_reward_enabled: t.cfg.free_play_first_touch_reward_enabled,
                    free_play_first_touch_reward_channel: t.cfg.free_play_first_touch_reward_channel.max(0) as i64,
                    free_play_bout_reward_enabled: t.cfg.free_play_bout_reward_enabled,
                    free_play_bout_reward_channel: t.cfg.free_play_bout_reward_channel.max(0) as i64,
                    free_play_bout_cooldown_s: t.cfg.free_play_bout_cooldown_s.max(0.0),
                    free_play_sustain_reward_enabled: t.cfg.free_play_sustain_reward_enabled,
                    free_play_sustain_reward_channel: t.cfg.free_play_sustain_reward_channel.max(0) as i64,
                    free_play_sustain_initial_delay_s: t.cfg.free_play_sustain_initial_delay_s.max(0.0),
                    free_play_sustain_interval_s: t.free_play_sustain_interval_s,
                    free_play_first_touch_reward_count: 0,
                    free_play_bout_reward_count: 0,
                    free_play_sustain_reward_count: 0,
                    free_play_total_reward_count: 0,
                    free_play_active_bout_count: 0,
                    free_play_total_active_time_s: 0.0,
                });
            }
            t.append_event(Event::new("free_play_start", now_s, now_s));
        }
        t
    }

    /// The initial "BehavState=intertrial" log (@2880). Call once before step().
    pub fn begin(&mut self) -> Vec<Effect> {
        vec![Effect::LogState("intertrial")]
    }

    // --- operator inputs (delivered between frames from the delegate) ---

    pub fn on_arrow_key(&mut self, key: &str, pressed: bool) {
        match key {
            "left" => self.operator_keys.left = pressed,
            "right" => self.operator_keys.right = pressed,
            "up" => self.operator_keys.up = pressed,
            "down" => self.operator_keys.down = pressed,
            _ => {}
        }
    }

    pub fn on_end_requested(&mut self, requested: bool) {
        self.free_play_end_requested = requested;
    }

    /// touch_handler (@2269-2277): ignores x<0.
    pub fn on_touch(&mut self, x: i64, y: i64, now_s: f64) {
        if x < 0 {
            return;
        }
        self.touch_detected_this_trial = true;
        self.last_touch_pos = (x, y);
        self.last_touch_time = Some(now_s);
    }

    /// analog_processor bookkeeping (@2320-2330): every sample lands in
    /// behav_result; the caller keeps the LAST one as the live (x, y).
    pub fn on_joystick_sample(&mut self, time_s: f64, x: f64, y: f64) {
        self.behav.append_joystick_sample(time_s, x, y);
    }

    // --- geometry (Python @2210-2225, int() = trunc) ---

    pub fn region_bounds_ratios(&self) -> (f64, f64, f64, f64) {
        let left = (self.region_x - self.region_w / 2.0)
            .max(0.0)
            .min(1.0 - self.region_w);
        let top = (self.region_y - self.region_h / 2.0)
            .max(0.0)
            .min(1.0 - self.region_h);
        (left, top, self.region_w, self.region_h)
    }

    pub fn to_region_pixels(&self, local_x: f64, local_y: f64, w: i64, h: i64) -> (i64, i64) {
        let (region_left, region_top, region_w, region_h) = self.region_bounds_ratios();
        let left_px = (region_left * w as f64) as i64;
        let top_px = (region_top * h as f64) as i64;
        let region_w_px = ((region_w * w as f64) as i64).max(1);
        let region_h_px = ((region_h * h as f64) as i64).max(1);
        let px = (left_px as f64 + local_x * region_w_px as f64) as i64;
        let py = (top_px as f64 + (1.0 - local_y) * region_h_px as f64) as i64;
        (px, py)
    }

    fn region_min_dim(&self, w: i64, h: i64) -> i64 {
        let (_, _, region_w, region_h) = self.region_bounds_ratios();
        let region_w_px = ((region_w * w as f64) as i64).max(1);
        let region_h_px = ((region_h * h as f64) as i64).max(1);
        region_w_px.min(region_h_px)
    }

    // --- internals ---

    fn apply_direction_influence(&self, raw_jx: f64, raw_jy: f64) -> (f64, f64) {
        let mut jx = raw_jx;
        let mut jy = raw_jy;
        if jx > 0.0 {
            jx *= self.influence.right;
        } else if jx < 0.0 {
            jx *= self.influence.left;
        }
        if jy > 0.0 {
            jy *= self.influence.up;
        } else if jy < 0.0 {
            jy *= self.influence.down;
        }
        (jx, jy)
    }

    fn get_operator_joystick(&self) -> (f64, f64) {
        let x = (self.operator_keys.right as i64 - self.operator_keys.left as i64) as f64;
        let y = (self.operator_keys.up as i64 - self.operator_keys.down as i64) as f64;
        (
            (x * KEYBOARD_JOYSTICK_MAGNITUDE).clamp(-1.0, 1.0),
            (y * KEYBOARD_JOYSTICK_MAGNITUDE).clamp(-1.0, 1.0),
        )
    }

    fn parse_target(&self, index: usize, target: &Value) -> Option<TargetSelection> {
        let obj = target.as_object()?;
        if !obj.get("enabled").and_then(Value::as_bool).unwrap_or(true) {
            return None;
        }
        let num = |k: &str, d: f64| {
            obj.get(k)
                .map(|v| clamp_float(Some(v), f64::NEG_INFINITY, f64::INFINITY, d))
                .unwrap_or(d)
        };
        Some(TargetSelection {
            index: index as i64,
            x: num("x_norm", 0.75).clamp(0.0, 1.0),
            y: num("y_norm", 0.50).clamp(0.0, 1.0),
            radius_ratio: num("radius_ratio", DEFAULT_TARGET_RADIUS_RATIO).clamp(0.01, 0.5),
            hold_time: num("hold_time", DEFAULT_TARGET_HOLD_TIME).clamp(0.01, 10.0),
            reward_channel: (num("reward_channel", self.cfg.reward_channel as f64) as i64).max(0),
            color: normalize_rgb(obj.get("target_color"), DEFAULT_TARGET_COLOR_I),
            opacity: clamp_float(obj.get("target_opacity"), 0.0, 1.0, DEFAULT_TARGET_OPACITY),
            active_color: normalize_rgb(obj.get("target_active_color"), DEFAULT_TARGET_ACTIVE_COLOR_I),
            active_opacity: clamp_float(
                obj.get("target_active_opacity"),
                0.0,
                1.0,
                DEFAULT_TARGET_ACTIVE_OPACITY,
            ),
            name: obj
                .get("name")
                .and_then(Value::as_str)
                .unwrap_or("")
                .to_string(),
            schedule_phase: "random".to_string(),
        })
    }

    /// place_target (@2512-2547), extended with the structured schedule.
    /// Every call advances the schedule cursor by exactly one shown target;
    /// callers must not invoke it speculatively.
    fn place_target(&mut self) -> TargetSelection {
        let enabled: Vec<TargetSelection> = self
            .cfg
            .targets
            .iter()
            .enumerate()
            .filter_map(|(i, t)| self.parse_target(i, t))
            .collect();
        if enabled.is_empty() {
            return TargetSelection {
                index: -1,
                x: 0.75,
                y: 0.50,
                radius_ratio: DEFAULT_TARGET_RADIUS_RATIO,
                hold_time: DEFAULT_TARGET_HOLD_TIME,
                reward_channel: self.cfg.reward_channel as i64,
                color: DEFAULT_TARGET_COLOR_I,
                opacity: DEFAULT_TARGET_OPACITY,
                active_color: DEFAULT_TARGET_ACTIVE_COLOR_I,
                active_opacity: DEFAULT_TARGET_ACTIVE_OPACITY,
                name: String::new(),
                schedule_phase: "random".to_string(),
            };
        }

        let mode = self.cfg.target_schedule.mode.as_str();
        // "random" (and any unknown mode): today's behavior, unchanged.
        if mode != "sequence" && mode != "center_out" {
            let i = self.rng.choice(enabled.len());
            return enabled[i].clone();
        }

        // Interleave roll: a random trial is INSERTED into the structured
        // stream — the schedule cursor does not advance, so the pattern
        // resumes where it left off. In center_out mode the roll only happens
        // on a pair boundary (when the next structured pick would be the
        // center), so an insert never splits a center -> peripheral pair.
        let ratio = self
            .cfg
            .target_schedule
            .interleave_random_ratio
            .clamp(0.0, 1.0);
        let at_pair_boundary = mode != "center_out" || self.scheduler.expect_center;
        if ratio > 0.0 && at_pair_boundary && self.rng.next_f64() < ratio {
            let i = self.rng.choice(enabled.len());
            return enabled[i].clone();
        }

        // Next structured pick, by target name. The cursor advances even if
        // the name turns out missing/disabled below: that slot just becomes a
        // random trial and the pattern continues at the next slot.
        let (name, phase) = if mode == "sequence" {
            let order = &self.cfg.target_schedule.order;
            if order.is_empty() {
                (String::new(), "sequence")
            } else {
                let name = order[(self.scheduler.seq_pos % order.len() as u64) as usize].clone();
                self.scheduler.seq_pos = self.scheduler.seq_pos.wrapping_add(1);
                (name, "sequence")
            }
        } else if self.scheduler.expect_center {
            self.scheduler.expect_center = false;
            (self.cfg.target_schedule.center.clone(), "center")
        } else {
            self.scheduler.expect_center = true;
            let center = &self.cfg.target_schedule.center;
            let ring: Vec<String> = if !self.cfg.target_schedule.peripherals.is_empty() {
                self.cfg.target_schedule.peripherals.clone()
            } else {
                enabled
                    .iter()
                    .map(|t| t.name.clone())
                    .filter(|n| !n.is_empty() && n != center)
                    .collect()
            };
            if ring.is_empty() {
                (String::new(), "peripheral")
            } else if self.cfg.target_schedule.peripheral_order == "random" {
                let i = self.rng.choice(ring.len());
                (ring[i].clone(), "peripheral")
            } else {
                let i = (self.scheduler.peripheral_pos % ring.len() as u64) as usize;
                self.scheduler.peripheral_pos = self.scheduler.peripheral_pos.wrapping_add(1);
                (ring[i].clone(), "peripheral")
            }
        };

        if !name.is_empty() {
            if let Some(t) = enabled.iter().find(|t| t.name == name) {
                let mut selected = t.clone();
                selected.schedule_phase = phase.to_string();
                return selected;
            }
        }
        // Name missing or disabled: fall back to a random draw for this slot.
        let i = self.rng.choice(enabled.len());
        enabled[i].clone()
    }

    fn ensure_next_target_preview(&mut self) {
        if self.next_target_preview.is_none() {
            self.next_target_preview = Some(self.place_target());
        }
    }

    fn consume_next_target(&mut self) -> TargetSelection {
        self.ensure_next_target_preview();
        // Lazily refilled by the next ensure call. An eager refill here would
        // advance the schedule cursor for a preview nothing reads, and the
        // Trial (rebuilt per run_trial stream) would discard it at trial end —
        // silently skipping one schedule slot per trial.
        self.next_target_preview.take().unwrap()
    }

    /// reset_attempt_tracking (@2373-2407).
    fn reset_attempt_tracking(&mut self, now_s: f64) {
        self.trial_index += 1;
        self.target_entry_count = 0;
        self.first_movement_time = None;
        self.first_target_entry_time = None;
        self.first_hold_start_time = None;
        self.previous_cursor_inside_target = false;
        self.current_attempt = Some(Attempt::new(
            self.trial_index,
            now_s,
            &self.cfg.control_mode,
            self.cfg.cursor_only_mode,
        ));
    }

    fn append_event(&mut self, event: Event) {
        if let Some(a) = self.current_attempt.as_mut() {
            a.events.push(event);
        }
    }

    fn event(&self, name: &str, now_s: f64) -> Event {
        let start = self
            .current_attempt
            .as_ref()
            .map(|a| a.start_time_perf_counter)
            .unwrap_or(now_s);
        Event::new(name, now_s, start)
    }

    /// finalize_attempt (@2420-2437).
    fn finalize_attempt(&mut self, outcome: &str, now_s: f64, failure_reason: Option<&str>) {
        let Some(mut a) = self.current_attempt.take() else { return };
        a.outcome = Some(outcome.to_string());
        a.failure_reason = failure_reason.map(str::to_string);
        a.joystick_active = self.joystick_active_this_trial;
        a.target_entry_count = self.target_entry_count;
        let start = a.start_time_perf_counter;
        let duration_s = (now_s - start).max(0.0);
        a.end = Some(AttemptEnd {
            end_time_perf_counter: now_s,
            duration_s,
            first_movement_time_s: self.first_movement_time.map(|t| (t - start).max(0.0)),
            first_target_entry_time_s: self.first_target_entry_time.map(|t| (t - start).max(0.0)),
            first_hold_start_time_s: self.first_hold_start_time.map(|t| (t - start).max(0.0)),
            success_time_s: (outcome == "success").then_some(duration_s),
        });
        self.behav.push_attempt(a);
    }

    fn reward_scale(&self) -> f64 {
        self.cfg.reward_scale.clamp(0.0, 100.0)
    }

    /// reset_intertrial_gate (@2476-2488).
    fn reset_intertrial_gate(&mut self, now_s: f64) {
        if self.cfg.require_center_before_trial {
            self.iti_end = f64::INFINITY;
            self.intertrial_center_ready = false;
        } else {
            self.iti_end = now_s + self.cfg.intertrial_interval;
            self.intertrial_center_ready = true;
        }
    }

    /// update_intertrial_gate (@2490-2505).
    fn update_intertrial_gate(&mut self, now_s: f64, cursor_centered: bool) {
        if !self.cfg.require_center_before_trial {
            return;
        }
        if cursor_centered {
            if !self.intertrial_center_ready {
                self.intertrial_center_ready = true;
                self.iti_end = now_s + self.cfg.intertrial_interval;
            }
            return;
        }
        self.intertrial_center_ready = false;
        self.iti_end = f64::INFINITY;
    }

    /// fail_for_touch_input (@2458-2474).
    fn fail_for_touch_input(&mut self, now_s: f64, out: &mut StepOutput) {
        let touch_time = self.last_touch_time.unwrap_or(now_s);
        let e = self
            .event("touch_input_fail", now_s)
            .with("touch_x", self.last_touch_pos.0)
            .with("touch_y", self.last_touch_pos.1)
            .with("touch_time_perf_counter", touch_time)
            .with(
                "touch_time_since_session_start_s",
                (touch_time - self.session_start).max(0.0),
            );
        self.append_event(e);
        self.streak_count = 0;
        self.finalize_attempt("fail", now_s, Some("touch_input"));
        out.log("fail");
        out.done = Some(TrialOutcome { success: false });
    }

    /// trigger_free_play_reward (@2575-2612).
    fn trigger_free_play_reward(
        &mut self,
        event_name: &str,
        now_s: f64,
        channel: i64,
        reward_kind: &str,
        analog_magnitude: f64,
        analog_x: f64,
        analog_y: f64,
        out: &mut StepOutput,
    ) {
        out.effects.push(Effect::Reward { channel, repeats: 1 });
        self.free_play_total_reward_count += 1;
        match reward_kind {
            "first_touch" => self.free_play_first_touch_reward_count += 1,
            "bout" => self.free_play_bout_reward_count += 1,
            "sustain" => self.free_play_sustain_reward_count += 1,
            _ => {}
        }
        let (ft, bo, su, tot) = (
            self.free_play_first_touch_reward_count,
            self.free_play_bout_reward_count,
            self.free_play_sustain_reward_count,
            self.free_play_total_reward_count,
        );
        if let Some(fp) = self
            .current_attempt
            .as_mut()
            .and_then(|a| a.free_play.as_mut())
        {
            fp.free_play_first_touch_reward_count = ft;
            fp.free_play_bout_reward_count = bo;
            fp.free_play_sustain_reward_count = su;
            fp.free_play_total_reward_count = tot;
        }
        let e = self
            .event(event_name, now_s)
            .with("reward_count", 1)
            .with("reward_channel", channel)
            .with("reward_kind", reward_kind)
            .with("total_free_play_reward_count", tot)
            .with("joystick_x", analog_x)
            .with("joystick_y", analog_y)
            .with("joystick_magnitude", analog_magnitude);
        self.append_event(e);
    }

    fn emit_free_play_active_end(&mut self, now_s: f64, analog: (f64, f64, f64)) {
        let bout_duration_s = self
            .free_play_active_bout_start
            .map(|t| (now_s - t).max(0.0))
            .unwrap_or(0.0);
        self.free_play_total_active_time_s += bout_duration_s;
        let total = self.free_play_total_active_time_s;
        if let Some(fp) = self
            .current_attempt
            .as_mut()
            .and_then(|a| a.free_play.as_mut())
        {
            fp.free_play_total_active_time_s = total;
        }
        let e = self
            .event("free_play_active_end", now_s)
            .with("active_bout_count", self.free_play_active_bout_count)
            .with("active_bout_duration_s", bout_duration_s)
            .with("total_active_time_s", total)
            .with("joystick_x", analog.0)
            .with("joystick_y", analog.1)
            .with("joystick_magnitude", analog.2);
        self.append_event(e);
        self.free_play_active_bout_start = None;
    }

    /// One frame. Mirrors the while-loop body @2881-3235.
    pub fn step(&mut self, inp: &StepInput) -> StepOutput {
        let mut out = StepOutput::default();
        let now = inp.now_s;
        let dt = (now - self.last_tick).clamp(0.0, 0.05);
        self.last_tick = now;

        // Success-pop visual hold: the reward/streak/log bookkeeping already
        // fired at hold-complete; here we simply keep the trial alive (frozen
        // cursor, target still drawn, pop expanding over it) so the pop renders,
        // then finish the trial once the pop duration elapses. Mirrors the
        // Python renderer's post-success wait (joystick_intro.py @4149-4156).
        if let Some(pop_start) = self.success_pop_start {
            let dur = self.cfg.success_pop_duration_s.clamp(0.0001, 1.0);
            let elapsed = now - pop_start;
            self.success_pop_ratio = (elapsed / dur).clamp(0.0, 1.0);
            if elapsed >= dur {
                out.done = Some(TrialOutcome { success: true });
            }
            return out;
        }

        let (operator_jx, operator_jy) = self.get_operator_joystick();
        let operator_override_active = operator_jx != 0.0 || operator_jy != 0.0;
        let analog_magnitude = inp.analog_x.hypot(inp.analog_y);
        let motion_floor = if self.cfg.zero_drift_mode {
            self.cfg.zero_drift_buffer
        } else {
            0.02
        };
        let analog_active = analog_magnitude >= motion_floor;

        // Cursor integration (@2890-2912).
        let (jx, jy);
        if operator_override_active {
            let (ojx, ojy) = self.apply_direction_influence(operator_jx, operator_jy);
            self.cursor_x += ojx * OPERATOR_KEYBOARD_CURSOR_SPEED * dt;
            self.cursor_y += ojy * OPERATOR_KEYBOARD_CURSOR_SPEED * dt;
            self.operator_cursor_latched = true;
            (jx, jy) = (ojx, ojy);
        } else if self.operator_cursor_latched && !analog_active {
            (jx, jy) = (0.0, 0.0);
        } else if self.cfg.control_mode == "direct" {
            let (djx, djy) = self.apply_direction_influence(inp.analog_x, inp.analog_y);
            if analog_active || self.cfg.direct_recenter_when_idle {
                self.cursor_x = 0.5 + djx * self.cfg.direct_range;
                self.cursor_y = 0.5 + djy * self.cfg.direct_range;
            }
            self.operator_cursor_latched = false;
            (jx, jy) = (djx, djy);
        } else {
            let (mut cjx, mut cjy) = self.apply_direction_influence(inp.analog_x, inp.analog_y);
            if self.cfg.zero_drift_mode && cjx.hypot(cjy) < self.cfg.zero_drift_buffer {
                cjx = 0.0;
                cjy = 0.0;
            }
            self.cursor_x += cjx * self.cfg.cumulative_speed * dt;
            self.cursor_y += cjy * self.cfg.cumulative_speed * dt;
            self.operator_cursor_latched = false;
            (jx, jy) = (cjx, cjy);
        }

        let joystick_is_active = jx.hypot(jy) >= motion_floor;

        self.cursor_x = self.cursor_x.clamp(0.0, 1.0);
        self.cursor_y = self.cursor_y.clamp(0.0, 1.0);

        // Pixel-space quantities (@2921-2937).
        let (w, h) = (inp.width_px, inp.height_px);
        let min_dim = self.region_min_dim(w, h);
        let target_radius_px = (self.current_target.radius_ratio * min_dim as f64) as i64;
        let (cursor_px_x, cursor_px_y) = self.to_region_pixels(self.cursor_x, self.cursor_y, w, h);
        let (target_px_x, target_px_y) = self.to_region_pixels(self.target_x, self.target_y, w, h);
        let (center_px_x, center_px_y) = self.to_region_pixels(0.5, 0.5, w, h);
        let center_gate_radius_px =
            ((self.cfg.center_gate_radius_ratio.clamp(0.001, 1.0) * min_dim as f64) as i64).max(1);
        let cursor_centered_for_next_trial = ((cursor_px_x - center_px_x) as f64)
            .hypot((cursor_px_y - center_px_y) as f64)
            <= center_gate_radius_px as f64;
        self.cursor_inside_target = false;
        self.hold_progress_ratio = 0.0;

        if self.cfg.cursor_only_mode {
            self.step_free_play(now, inp, analog_magnitude, &mut out);
        } else if self.state == State::Intertrial {
            self.update_intertrial_gate(now, cursor_centered_for_next_trial);
            if now >= self.iti_end {
                self.start_trial(now, &mut out);
            }
        } else {
            self.step_target_trial(
                now,
                (jx, jy),
                joystick_is_active,
                (cursor_px_x, cursor_px_y),
                (target_px_x, target_px_y),
                target_radius_px,
                &mut out,
            );
        }
        out
    }

    /// The intertrial -> start_on transition (@3060-3122).
    fn start_trial(&mut self, now: f64, out: &mut StepOutput) {
        let target = self.consume_next_target();
        self.current_target = target.clone();
        self.target_x = target.x;
        self.target_y = target.y;
        self.hold_start = None;
        self.trial_start = now;
        self.reset_attempt_tracking(now);
        self.joystick_active_this_trial = false;
        self.touch_detected_this_trial = false;
        self.last_touch_pos = (0, 0);
        self.last_touch_time = None;
        self.state = State::StartOn;
        self.state_brightness = toggle_brightness(self.state_brightness);
        if let Some(a) = self.current_attempt.as_mut() {
            a.target_index = Some(target.index);
            a.target_position = Some(TargetPosition {
                x_norm: target.x,
                y_norm: target.y,
            });
            a.target_radius_ratio = Some(target.radius_ratio);
            a.hold_time_s = Some(target.hold_time);
            a.reward_channel = Some(target.reward_channel);
            a.target_color_rgb = Some(target.color);
            a.target_opacity = Some(target.opacity);
            a.target_active_color_rgb = Some(target.active_color);
            a.target_active_opacity = Some(target.active_opacity);
            a.schedule_phase = Some(target.schedule_phase.clone());
        }
        let e = self
            .event("target_on", now)
            .with("target_index", target.index)
            .with("target_x", target.x)
            .with("target_y", target.y)
            .with("target_radius_ratio", target.radius_ratio)
            .with("hold_time_s", target.hold_time)
            .with("reward_channel", target.reward_channel)
            .with("target_color_rgb", target.color.to_vec())
            .with("target_opacity", target.opacity)
            .with("target_active_color_rgb", target.active_color.to_vec())
            .with("target_active_opacity", target.active_opacity)
            .with("schedule_phase", target.schedule_phase.clone());
        self.append_event(e);
        out.log("start_on");
    }

    /// The start_on branch (@3123-3232).
    #[allow(clippy::too_many_arguments)]
    fn step_target_trial(
        &mut self,
        now: f64,
        (jx, jy): (f64, f64),
        joystick_is_active: bool,
        (cursor_px_x, cursor_px_y): (i64, i64),
        (target_px_x, target_px_y): (i64, i64),
        target_radius_px: i64,
        out: &mut StepOutput,
    ) {
        if self.cfg.fail_on_touch_input && self.touch_detected_this_trial {
            return self.fail_for_touch_input(now, out);
        }

        if joystick_is_active && !self.joystick_active_this_trial {
            self.ignored_idle_trial_count = 0;
            self.first_movement_time = Some(now);
            let e = self
                .event("first_joystick_movement", now)
                .with("joystick_x", jx)
                .with("joystick_y", jy);
            self.append_event(e);
        }
        if joystick_is_active {
            self.joystick_active_this_trial = true;
            if let Some(a) = self.current_attempt.as_mut() {
                a.joystick_active = true;
            }
        }

        let dist_to_target = ((cursor_px_x - target_px_x) as f64)
            .hypot((cursor_px_y - target_px_y) as f64);
        self.cursor_inside_target = dist_to_target <= target_radius_px as f64;

        if self.cursor_inside_target && !self.previous_cursor_inside_target {
            self.target_entry_count += 1;
            if self.first_target_entry_time.is_none() {
                self.first_target_entry_time = Some(now);
            }
            let e = self
                .event("target_entry", now)
                .with("cursor_x", self.cursor_x)
                .with("cursor_y", self.cursor_y)
                .with("entry_count", self.target_entry_count);
            self.append_event(e);
        }

        if self.cursor_inside_target {
            match self.hold_start {
                None => {
                    self.hold_start = Some(now);
                    if self.first_hold_start_time.is_none() {
                        self.first_hold_start_time = Some(now);
                    }
                    let e = self
                        .event("hold_start", now)
                        .with("cursor_x", self.cursor_x)
                        .with("cursor_y", self.cursor_y)
                        .with("entry_count", self.target_entry_count);
                    self.append_event(e);
                    self.hold_progress_ratio = 0.0;
                }
                Some(hold_start) if now - hold_start >= self.current_target.hold_time => {
                    self.hold_progress_ratio = 1.0;
                    let e = self
                        .event("hold_complete", now)
                        .with("hold_duration_s", now - hold_start);
                    self.append_event(e);
                    let channel = self.current_target.reward_channel;
                    out.effects.push(Effect::Reward { channel, repeats: 1 });
                    let e = self
                        .event("reward_triggered", now)
                        .with("reward_count", 1)
                        .with("reward_channel", channel)
                        .with("reward_scale", self.reward_scale());
                    self.append_event(e);
                    self.streak_count += 1;
                    let bonus_hit = self.task_animation_enabled
                        && self.cfg.streak_bonus_threshold > 0
                        && self.streak_count % self.cfg.streak_bonus_threshold == 0;
                    if bonus_hit {
                        let repeats = self.cfg.streak_bonus_reward_count.max(1);
                        out.effects.push(Effect::Reward { channel, repeats });
                        let e = self
                            .event("bonus_reward_triggered", now)
                            .with("reward_count", repeats)
                            .with("reward_channel", channel)
                            .with("reward_scale", self.reward_scale());
                        self.append_event(e);
                        if self.cfg.streak_reset_on_bonus {
                            self.streak_count = 0;
                        }
                    }
                    let e = self
                        .event("success", now)
                        .with("streak_count", self.streak_count);
                    self.append_event(e);
                    self.finalize_attempt("success", now, None);
                    out.log("success");
                    // If the success pop is enabled, defer trial completion so
                    // the pop actually renders (see the step() short-circuit).
                    // The reward and success marker above already fired, so the
                    // operator sound plays at pop start. Otherwise finish now.
                    let target_animation_enabled =
                        self.cfg.animations_enabled && self.cfg.target_animation_enabled;
                    if target_animation_enabled
                        && self.cfg.show_success_pop
                        && self.cfg.success_pop_duration_s > 0.0
                    {
                        self.success_pop_start = Some(now);
                        self.success_pop_ratio = 0.0;
                        self.success_pop_x = self.target_x;
                        self.success_pop_y = self.target_y;
                    } else {
                        out.done = Some(TrialOutcome { success: true });
                    }
                    return;
                }
                Some(hold_start) => {
                    self.hold_progress_ratio =
                        ((now - hold_start) / self.current_target.hold_time.max(0.001)).clamp(0.0, 1.0);
                }
            }
        } else {
            if self.previous_cursor_inside_target {
                let e = self
                    .event("target_exit", now)
                    .with("cursor_x", self.cursor_x)
                    .with("cursor_y", self.cursor_y)
                    .with("entry_count", self.target_entry_count);
                self.append_event(e);
            }
            if let Some(hold_start) = self.hold_start {
                let e = self
                    .event("hold_break", now)
                    .with("hold_duration_s", now - hold_start);
                self.append_event(e);
            }
            self.hold_start = None;
            self.hold_progress_ratio = 0.0;
        }
        self.previous_cursor_inside_target = self.cursor_inside_target;

        // Timeout (@3205-3232).
        if now - self.trial_start >= self.cfg.trial_timeout {
            if self.cfg.ignore_idle_trial_failures && !self.joystick_active_this_trial {
                self.ignored_idle_trial_count += 1;
                self.hold_start = None;
                self.state = State::Intertrial;
                self.reset_intertrial_gate(now);
                self.state_brightness = 0;
                let e = self
                    .event("ignored_idle_timeout", now)
                    .with("ignored_idle_trial_count", self.ignored_idle_trial_count);
                self.append_event(e);
                self.finalize_attempt("ignored_idle", now, Some("timeout_without_movement"));
                if self.ignored_idle_trial_count > self.cfg.ignored_idle_sample_clear_threshold.max(0)
                {
                    let count = self.ignored_idle_trial_count;
                    self.behav.clear_samples_for_idle(now, count);
                }
                out.log("intertrial");
                return;
            }
            self.streak_count = 0;
            let reason = if !self.joystick_active_this_trial {
                "timeout_without_movement"
            } else {
                "timeout_after_movement"
            };
            let e = self.event("fail", now).with("failure_reason", reason);
            self.append_event(e);
            self.finalize_attempt("fail", now, Some(reason));
            out.log("fail");
            out.done = Some(TrialOutcome { success: false });
        }
    }

    /// The cursor_only free-play branch (@2926-3057).
    fn step_free_play(
        &mut self,
        now: f64,
        inp: &StepInput,
        analog_magnitude: f64,
        out: &mut StepOutput,
    ) {
        if self.cfg.fail_on_touch_input && self.touch_detected_this_trial {
            return self.fail_for_touch_input(now, out);
        }
        let analog = (inp.analog_x, inp.analog_y, analog_magnitude);

        let free_play_is_active = analog_magnitude >= self.free_play_active_threshold;
        let free_play_started = free_play_is_active && !self.free_play_was_active;
        let free_play_ended = !free_play_is_active && self.free_play_was_active;

        if free_play_started {
            self.free_play_active_bout_start = Some(now);
            self.free_play_active_bout_count += 1;
            self.free_play_last_sustain_reward_time = None;
            let count = self.free_play_active_bout_count;
            if let Some(fp) = self
                .current_attempt
                .as_mut()
                .and_then(|a| a.free_play.as_mut())
            {
                fp.free_play_active_bout_count = count;
            }
            let e = self
                .event("free_play_active_start", now)
                .with("active_bout_count", count)
                .with("joystick_x", analog.0)
                .with("joystick_y", analog.1)
                .with("joystick_magnitude", analog.2);
            self.append_event(e);
        }

        if free_play_is_active && !self.joystick_active_this_trial {
            self.first_movement_time = Some(now);
            let e = self
                .event("first_joystick_movement", now)
                .with("joystick_x", analog.0)
                .with("joystick_y", analog.1)
                .with("joystick_magnitude", analog.2);
            self.append_event(e);
        }
        if free_play_is_active {
            self.joystick_active_this_trial = true;
            if let Some(a) = self.current_attempt.as_mut() {
                a.joystick_active = true;
            }
        }

        if free_play_started
            && self.cfg.free_play_first_touch_reward_enabled
            && !self.free_play_first_touch_delivered
        {
            let ch = self.cfg.free_play_first_touch_reward_channel.max(0) as i64;
            self.trigger_free_play_reward(
                "free_play_first_touch_reward_triggered",
                now,
                ch,
                "first_touch",
                analog.2,
                analog.0,
                analog.1,
                out,
            );
            self.free_play_first_touch_delivered = true;
        }

        let bout_cooldown_elapsed = self
            .free_play_last_bout_reward_time
            .map(|t| now - t >= self.cfg.free_play_bout_cooldown_s.max(0.0))
            .unwrap_or(true);
        if free_play_started && self.cfg.free_play_bout_reward_enabled && bout_cooldown_elapsed {
            let ch = self.cfg.free_play_bout_reward_channel.max(0) as i64;
            self.trigger_free_play_reward(
                "free_play_bout_reward_triggered",
                now,
                ch,
                "bout",
                analog.2,
                analog.0,
                analog.1,
                out,
            );
            self.free_play_last_bout_reward_time = Some(now);
        }

        if free_play_is_active && self.cfg.free_play_sustain_reward_enabled {
            if let Some(bout_start) = self.free_play_active_bout_start {
                let active_duration_s = (now - bout_start).max(0.0);
                let delay_elapsed =
                    active_duration_s >= self.cfg.free_play_sustain_initial_delay_s.max(0.0);
                let interval_elapsed = self
                    .free_play_last_sustain_reward_time
                    .map(|t| now - t >= self.free_play_sustain_interval_s)
                    .unwrap_or(true);
                if delay_elapsed && interval_elapsed {
                    let ch = self.cfg.free_play_sustain_reward_channel.max(0) as i64;
                    self.trigger_free_play_reward(
                        "free_play_sustain_reward_triggered",
                        now,
                        ch,
                        "sustain",
                        analog.2,
                        analog.0,
                        analog.1,
                        out,
                    );
                    self.free_play_last_sustain_reward_time = Some(now);
                }
            }
        }

        if free_play_ended {
            self.emit_free_play_active_end(now, analog);
            self.free_play_last_sustain_reward_time = None;
        }
        self.free_play_was_active = free_play_is_active;

        if self.free_play_end_requested {
            if free_play_is_active && self.free_play_active_bout_start.is_some() {
                self.emit_free_play_active_end(now, analog);
            }
            let e = self.event("free_play_end_requested", now);
            self.append_event(e);
            self.finalize_attempt("success", now, None);
            out.log("success");
            out.done = Some(TrialOutcome { success: true });
        }
    }

    /// Operator-only trial metadata, streamed by the render loop as a
    /// `TrialInfo=` marker right after `start_on` (see render::step_active).
    /// It rides the delegate event stream ONLY — never the Thalamus log, and
    /// never the subject display. `mode` lets the operator HUD distinguish
    /// "random mode" from "random insert into a structured stream".
    pub fn trial_info_json(&self) -> String {
        serde_json::json!({
            "schedule_phase": self.current_target.schedule_phase,
            "target_name": self.current_target.name,
            "mode": self.cfg.target_schedule.mode,
            "interleave": self.cfg.target_schedule.interleave_random_ratio.clamp(0.0, 1.0),
        })
        .to_string()
    }

    /// Cross-trial bookkeeping to persist into task_config (Python mutates
    /// task_config directly @3155/@3237-3239; we ship it as one JSON object in
    /// TrialEvent.config_updates_json for the delegate to apply).
    pub fn config_updates(&self) -> String {
        serde_json::json!({
            "_streak_count": self.streak_count,
            "_last_cursor_x": self.cursor_x,
            "_last_cursor_y": self.cursor_y,
            "_operator_keys_pressed": {
                "left": self.operator_keys.left,
                "right": self.operator_keys.right,
                "up": self.operator_keys.up,
                "down": self.operator_keys.down,
            },
            "_schedule_seq_pos": self.scheduler.seq_pos,
            "_schedule_expect_center": self.scheduler.expect_center,
            "_schedule_peripheral_pos": self.scheduler.peripheral_pos,
        })
        .to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cfg(json: &str) -> (TaskConfig, Value) {
        let raw: Value = serde_json::from_str(json).unwrap();
        (TaskConfig::from_json(json).unwrap(), raw)
    }

    fn step_at(t: &mut Trial, now: f64, x: f64, y: f64) -> StepOutput {
        t.step(&StepInput {
            now_s: now,
            analog_x: x,
            analog_y: y,
            width_px: 1920,
            height_px: 1080,
        })
    }

    #[test]
    fn intertrial_transitions_to_start_on_after_iti() {
        let (c, raw) = cfg(r#"{"intertrial_interval": 1.0, "trial_timeout": 5.0}"#);
        let mut t = Trial::new(c, &raw, 100.0, 42);
        assert_eq!(t.begin(), vec![Effect::LogState("intertrial")]);
        let out = step_at(&mut t, 100.5, 0.0, 0.0);
        assert!(out.effects.is_empty());
        assert_eq!(t.state, State::Intertrial);
        let out = step_at(&mut t, 101.0, 0.0, 0.0);
        assert_eq!(out.effects, vec![Effect::LogState("start_on")]);
        assert_eq!(t.state, State::StartOn);
        assert_eq!(t.state_brightness, 255);
        // target_on event recorded with the full extras payload.
        let a = t.current_attempt.as_ref().unwrap();
        assert_eq!(a.events[0].name, "target_on");
        assert_eq!(a.attempt_index, 1);
    }

    #[test]
    fn default_target_success_path_delivers_reward_and_finalizes() {
        // Default target at (0.75, 0.50), radius 0.08, hold 0.4 s. Direct mode:
        // cursor = 0.5 + j*0.45, so analog (0.5556, 0.0) puts cursor at 0.75.
        let (c, raw) = cfg(
            r#"{"intertrial_interval": 0.1, "trial_timeout": 10.0, "control_mode": "direct"}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.1, 0.0, 0.0); // -> start_on
        assert_eq!(t.state, State::StartOn);
        // Move into the target and hold.
        let mut now = 0.11;
        let mut done = None;
        let mut rewards = 0;
        while now < 2.0 {
            let out = step_at(&mut t, now, 0.5556, 0.0);
            rewards += out
                .effects
                .iter()
                .filter(|e| matches!(e, Effect::Reward { .. }))
                .count();
            if let Some(d) = out.done {
                done = Some((d, out.effects));
                break;
            }
            now += 1.0 / 240.0;
        }
        let (outcome, final_effects) = done.expect("trial should complete");
        assert!(outcome.success);
        assert_eq!(rewards, 1);
        assert!(final_effects.contains(&Effect::LogState("success")));
        assert_eq!(t.behav.final_outcome.as_deref(), Some("success"));
        let attempt = t.behav.final_attempt.as_ref().unwrap();
        let names: Vec<&str> = attempt.events.iter().map(|e| e.name.as_str()).collect();
        assert_eq!(
            names,
            vec![
                "target_on",
                "first_joystick_movement",
                "target_entry",
                "hold_start",
                "hold_complete",
                "reward_triggered",
                "success"
            ]
        );
        assert_eq!(t.streak_count, 1);
        let end = attempt.end.as_ref().unwrap();
        assert!(end.success_time_s.is_some());
        assert!(end.first_movement_time_s.is_some());
    }

    #[test]
    fn success_pop_defers_completion_until_pop_duration_elapses() {
        // With the pop enabled, hold-complete fires the reward + success marker
        // but keeps the trial alive so the pop renders; the trial finishes only
        // after success_pop_duration_s.
        let (c, raw) = cfg(
            r#"{"intertrial_interval": 0.1, "trial_timeout": 10.0, "control_mode": "direct",
                "animations_enabled": true, "target_animation_enabled": true,
                "show_success_pop": true, "success_pop_duration_s": 0.2}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.1, 0.0, 0.0); // -> start_on

        // Hold to completion; capture the frame where reward fires.
        let mut now = 0.11;
        let mut rewards = 0;
        let mut hold_complete_now = None;
        while now < 2.0 {
            let out = step_at(&mut t, now, 0.5556, 0.0);
            let this_reward = out
                .effects
                .iter()
                .any(|e| matches!(e, Effect::Reward { .. }));
            if this_reward {
                rewards += 1;
                // Reward fires at hold-complete: success logged, pop armed, but
                // the trial has NOT finished yet.
                assert!(out.effects.contains(&Effect::LogState("success")));
                assert!(out.done.is_none(), "completion must be deferred for the pop");
                assert!(t.success_pop_start.is_some());
                hold_complete_now = Some(now);
                break;
            }
            assert!(out.done.is_none());
            now += 1.0 / 240.0;
        }
        let hc = hold_complete_now.expect("hold should complete");
        assert_eq!(rewards, 1);

        // A frame inside the pop window keeps the trial alive and advances the
        // pop ratio without re-firing the reward.
        let out = step_at(&mut t, hc + 0.1, 0.5556, 0.0);
        assert!(out.done.is_none());
        assert!(out.effects.is_empty(), "pop hold must not re-fire effects");
        assert!(t.success_pop_ratio > 0.0 && t.success_pop_ratio < 1.0);

        // Past the pop duration the trial finishes as a success.
        let out = step_at(&mut t, hc + 0.25, 0.5556, 0.0);
        assert_eq!(out.done.map(|d| d.success), Some(true));
        assert_eq!(t.streak_count, 1);
    }

    #[test]
    fn timeout_without_movement_fails_with_reason() {
        let (c, raw) = cfg(r#"{"intertrial_interval": 0.1, "trial_timeout": 0.5}"#);
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.1, 0.0, 0.0); // -> start_on
        let out = step_at(&mut t, 0.7, 0.0, 0.0); // past timeout
        assert_eq!(out.done.unwrap().success, false);
        assert!(out.effects.contains(&Effect::LogState("fail")));
        let attempt = t.behav.final_attempt.as_ref().unwrap();
        assert_eq!(attempt.failure_reason.as_deref(), Some("timeout_without_movement"));
        assert_eq!(t.streak_count, 0);
    }

    #[test]
    fn ignored_idle_rearms_intertrial_without_ending() {
        let (c, raw) = cfg(
            r#"{"intertrial_interval": 0.2, "trial_timeout": 0.5, "ignore_idle_trial_failures": true}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.2, 0.0, 0.0); // -> start_on
        let out = step_at(&mut t, 0.71, 0.0, 0.0); // idle timeout
        assert!(out.done.is_none());
        assert!(out.effects.contains(&Effect::LogState("intertrial")));
        assert_eq!(t.state, State::Intertrial);
        assert_eq!(t.state_brightness, 0);
        assert_eq!(t.behav.final_outcome.as_deref(), Some("ignored_idle"));
        // Re-arms: next ITI expiry starts attempt 2.
        let out = step_at(&mut t, 0.92, 0.0, 0.0);
        assert!(out.effects.contains(&Effect::LogState("start_on")));
        assert_eq!(t.behav.attempts.len(), 1);
        assert_eq!(t.current_attempt.as_ref().unwrap().attempt_index, 2);
    }

    #[test]
    fn cumulative_mode_integrates_and_zero_drift_deadbands() {
        let (c, raw) = cfg(
            r#"{"control_mode": "cumulative", "cumulative_speed": 0.5, "zero_drift_mode": true,
                "zero_drift_buffer": 0.05, "intertrial_interval": 99.0}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        // Below deadband: no motion.
        step_at(&mut t, 0.01, 0.03, 0.0);
        assert_eq!(t.cursor_x, 0.5);
        // Above deadband: integrates dx = 1.0 * 0.5 * dt.
        step_at(&mut t, 0.02, 1.0, 0.0);
        assert!((t.cursor_x - (0.5 + 0.5 * 0.01)).abs() < 1e-12);
    }

    #[test]
    fn direct_mode_recenter_when_idle_toggles() {
        let (c, raw) = cfg(
            r#"{"control_mode": "direct", "direct_recenter_when_idle": false,
                "direct_range": 0.4, "intertrial_interval": 99.0}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.01, 0.5, 0.0);
        assert!((t.cursor_x - 0.7).abs() < 1e-12);
        // Idle (below threshold) with recenter off: cursor stays.
        step_at(&mut t, 0.02, 0.0, 0.0);
        assert!((t.cursor_x - 0.7).abs() < 1e-12);
    }

    #[test]
    fn operator_override_latches_until_analog_active() {
        let (c, raw) = cfg(r#"{"control_mode": "direct", "intertrial_interval": 99.0}"#);
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        t.on_arrow_key("right", true);
        step_at(&mut t, 0.01, 0.0, 0.0);
        let moved = t.cursor_x;
        assert!(moved > 0.5);
        t.on_arrow_key("right", false);
        // Latched: idle analog does NOT recenter the cursor.
        step_at(&mut t, 0.02, 0.0, 0.0);
        assert_eq!(t.cursor_x, moved);
        // Analog activity releases the latch (direct mode resumes).
        step_at(&mut t, 0.03, 0.5, 0.0);
        assert!((t.cursor_x - (0.5 + 0.5 * 0.45)).abs() < 1e-12);
    }

    #[test]
    fn free_play_bout_and_end_flow() {
        let (c, raw) = cfg(
            r#"{"cursor_only_mode": true, "free_play_bout_reward_enabled": true,
                "free_play_bout_cooldown_s": 0.0, "free_play_active_threshold": 0.1}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        // Bout starts -> bout reward.
        let out = step_at(&mut t, 0.01, 0.5, 0.0);
        assert!(out
            .effects
            .iter()
            .any(|e| matches!(e, Effect::Reward { .. })));
        // Bout ends.
        step_at(&mut t, 0.5, 0.0, 0.0);
        // Operator ends free play.
        t.on_end_requested(true);
        let out = step_at(&mut t, 0.6, 0.0, 0.0);
        assert!(out.done.unwrap().success);
        let attempt = t.behav.final_attempt.as_ref().unwrap();
        let names: Vec<&str> = attempt.events.iter().map(|e| e.name.as_str()).collect();
        assert_eq!(
            names,
            vec![
                "free_play_start",
                "free_play_active_start",
                "first_joystick_movement",
                "free_play_bout_reward_triggered",
                "free_play_active_end",
                "free_play_end_requested"
            ]
        );
        let fp = attempt.free_play.as_ref().unwrap();
        assert_eq!(fp.free_play_bout_reward_count, 1);
        assert_eq!(fp.free_play_active_bout_count, 1);
        assert!(fp.free_play_total_active_time_s > 0.0);
    }

    #[test]
    fn touch_fail_when_enabled() {
        let (c, raw) = cfg(
            r#"{"fail_on_touch_input": true, "intertrial_interval": 0.1, "trial_timeout": 10.0}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.1, 0.0, 0.0); // -> start_on
        t.on_touch(300, 400, 0.15);
        let out = step_at(&mut t, 0.2, 0.0, 0.0);
        assert_eq!(out.done.unwrap().success, false);
        let attempt = t.behav.final_attempt.as_ref().unwrap();
        assert_eq!(attempt.failure_reason.as_deref(), Some("touch_input"));
        let touch_event = attempt.events.iter().find(|e| e.name == "touch_input_fail").unwrap();
        assert_eq!(touch_event.extra["touch_x"], 300);
    }

    #[test]
    fn streak_persists_via_config_updates() {
        let (c, raw) = cfg(r#"{"_streak_count": 3, "intertrial_interval": 99.0}"#);
        let t = Trial::new(c, &raw, 0.0, 7);
        assert_eq!(t.streak_count, 3);
        let updates: Value = serde_json::from_str(&t.config_updates()).unwrap();
        assert_eq!(updates["_streak_count"], 3);
        assert_eq!(updates["_last_cursor_x"], 0.5);
    }

    #[test]
    fn configured_targets_choose_among_enabled() {
        let (c, raw) = cfg(
            r#"{"intertrial_interval": 0.1, "targets": [
                {"x_norm": 0.2, "y_norm": 0.3, "enabled": false},
                {"x_norm": 0.9, "y_norm": 0.8, "radius_ratio": 0.2, "hold_time": 1.5,
                 "reward_channel": 2, "target_color": [10, 20, 30]}
            ]}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.1, 0.0, 0.0); // -> start_on picks the only enabled target
        assert_eq!(t.current_target.index, 1);
        assert_eq!(t.current_target.x, 0.9);
        assert_eq!(t.current_target.hold_time, 1.5);
        assert_eq!(t.current_target.reward_channel, 2);
        assert_eq!(t.current_target.color, [10, 20, 30]);
    }

    // --- structured target schedule ---

    /// Four named targets on a cross, plus config fragments merged in.
    fn sched_cfg(schedule_json: &str, extra: &str) -> (TaskConfig, Value) {
        let json = format!(
            r#"{{"intertrial_interval": 0.1, "targets": [
                {{"name": "C",  "x_norm": 0.5, "y_norm": 0.5}},
                {{"name": "U1", "x_norm": 0.5, "y_norm": 0.9}},
                {{"name": "R1", "x_norm": 0.9, "y_norm": 0.5}},
                {{"name": "D1", "x_norm": 0.5, "y_norm": 0.1}}
            ], "target_schedule": {schedule_json}{extra}}}"#
        );
        cfg(&json)
    }

    fn picks(t: &mut Trial, n: usize) -> Vec<(String, String)> {
        (0..n)
            .map(|_| {
                let s = t.consume_next_target();
                (s.name, s.schedule_phase)
            })
            .collect()
    }

    #[test]
    fn sequence_mode_cycles_order() {
        let (c, raw) = sched_cfg(r#"{"mode": "sequence", "order": ["U1", "R1", "D1"]}"#, "");
        let mut t = Trial::new(c, &raw, 0.0, 42);
        let got = picks(&mut t, 7);
        let names: Vec<&str> = got.iter().map(|(n, _)| n.as_str()).collect();
        assert_eq!(names, vec!["U1", "R1", "D1", "U1", "R1", "D1", "U1"]);
        assert!(got.iter().all(|(_, p)| p == "sequence"));
    }

    #[test]
    fn center_out_alternates_and_cycles_ring_sequentially() {
        let (c, raw) = sched_cfg(
            r#"{"mode": "center_out", "center": "C", "peripheral_order": "sequential"}"#,
            "",
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        let got = picks(&mut t, 8);
        // Ring defaults to all enabled non-center names, in target-list order.
        let expect = [
            ("C", "center"),
            ("U1", "peripheral"),
            ("C", "center"),
            ("R1", "peripheral"),
            ("C", "center"),
            ("D1", "peripheral"),
            ("C", "center"),
            ("U1", "peripheral"),
        ];
        for (i, (name, phase)) in got.iter().enumerate() {
            assert_eq!((name.as_str(), phase.as_str()), expect[i], "pick {i}");
        }
    }

    #[test]
    fn center_out_random_stays_within_ring() {
        let (c, raw) = sched_cfg(
            r#"{"mode": "center_out", "center": "C", "peripherals": ["U1", "D1"],
                "peripheral_order": "random"}"#,
            "",
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        for (i, (name, phase)) in picks(&mut t, 20).iter().enumerate() {
            if i % 2 == 0 {
                assert_eq!((name.as_str(), phase.as_str()), ("C", "center"));
            } else {
                assert!(name == "U1" || name == "D1", "pick {i} left the ring: {name}");
                assert_eq!(phase, "peripheral");
            }
        }
    }

    #[test]
    fn interleave_zero_never_inserts_random() {
        let (c, raw) = sched_cfg(
            r#"{"mode": "sequence", "order": ["U1", "R1"], "interleave_random_ratio": 0.0}"#,
            "",
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        let got = picks(&mut t, 10);
        assert!(got.iter().all(|(_, p)| p == "sequence"));
        let names: Vec<&str> = got.iter().map(|(n, _)| n.as_str()).collect();
        assert_eq!(names, vec!["U1", "R1", "U1", "R1", "U1", "R1", "U1", "R1", "U1", "R1"]);
    }

    #[test]
    fn interleave_one_is_all_random_and_cursor_never_advances() {
        let (c, raw) = sched_cfg(
            r#"{"mode": "sequence", "order": ["U1", "R1"], "interleave_random_ratio": 1.0}"#,
            "",
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        let got = picks(&mut t, 10);
        assert!(got.iter().all(|(_, p)| p == "random"), "got: {got:?}");
        assert_eq!(t.scheduler.seq_pos, 0);
    }

    #[test]
    fn interleave_resumes_pattern_after_random_insertion() {
        let (c, raw) = sched_cfg(
            r#"{"mode": "sequence", "order": ["U1", "R1", "D1"],
                "interleave_random_ratio": 0.5}"#,
            "",
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        // Regardless of where random insertions land, the structured
        // subsequence must be exactly the order cycling in-order.
        let structured: Vec<String> = picks(&mut t, 40)
            .into_iter()
            .filter(|(_, p)| p == "sequence")
            .map(|(n, _)| n)
            .collect();
        assert!(!structured.is_empty());
        for (i, name) in structured.iter().enumerate() {
            assert_eq!(name, ["U1", "R1", "D1"][i % 3], "structured pick {i}");
        }
    }

    #[test]
    fn center_out_inserts_never_split_a_pair() {
        // Even at a high insert ratio, every center pick must be immediately
        // followed by its peripheral: inserts only land between pairs.
        let (c, raw) = sched_cfg(
            r#"{"mode": "center_out", "center": "C", "peripheral_order": "random",
                "interleave_random_ratio": 0.7}"#,
            "",
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        let got = picks(&mut t, 200);
        let inserts = got.iter().filter(|(_, p)| p == "random").count();
        assert!(inserts > 0, "ratio 0.7 should produce inserts");
        for (i, (_, phase)) in got.iter().enumerate() {
            if phase == "center" {
                assert!(i + 1 < got.len(), "stream should not end mid-pair here");
                assert_eq!(
                    got[i + 1].1, "peripheral",
                    "pick {} split a pair: {:?} then {:?}", i, got[i], got[i + 1]
                );
            }
        }
    }

    #[test]
    fn center_out_interleave_one_is_still_all_random() {
        // expect_center starts true (a pair boundary) and the cursor never
        // advances, so ratio 1.0 stays all-random in center_out too.
        let (c, raw) = sched_cfg(
            r#"{"mode": "center_out", "center": "C", "interleave_random_ratio": 1.0}"#,
            "",
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        let got = picks(&mut t, 10);
        assert!(got.iter().all(|(_, p)| p == "random"), "got: {got:?}");
        assert!(t.scheduler.expect_center);
    }

    #[test]
    fn missing_or_disabled_name_falls_back_to_random() {
        // "GONE" is not a target; C is disabled -> center picks fall back too.
        let (c, raw) = cfg(
            r#"{"targets": [
                {"name": "C", "enabled": false},
                {"name": "U1", "x_norm": 0.5, "y_norm": 0.9}
            ], "target_schedule": {"mode": "sequence", "order": ["GONE", "U1"]}}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        let got = picks(&mut t, 4);
        assert_eq!(got[0], ("U1".to_string(), "random".to_string())); // GONE -> fallback
        assert_eq!(got[1], ("U1".to_string(), "sequence".to_string()));
        assert_eq!(got[2], ("U1".to_string(), "random".to_string()));
        assert_eq!(got[3], ("U1".to_string(), "sequence".to_string()));
    }

    #[test]
    fn random_mode_tags_random_and_ignores_schedule_state() {
        let (c, raw) = sched_cfg(r#"{"mode": "random", "interleave_random_ratio": 1.0}"#, "");
        let mut t = Trial::new(c, &raw, 0.0, 42);
        for (name, phase) in picks(&mut t, 10) {
            assert_eq!(phase, "random");
            assert!(["C", "U1", "R1", "D1"].contains(&name.as_str()));
        }
        assert_eq!(t.scheduler.seq_pos, 0);
    }

    #[test]
    fn scheduler_state_round_trips_via_config_updates() {
        let (c, raw) = sched_cfg(
            r#"{"mode": "sequence", "order": ["U1", "R1", "D1"]}"#,
            r#", "_schedule_seq_pos": 2, "_schedule_expect_center": false,
               "_schedule_peripheral_pos": 1"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        // Restored cursor: next sequence pick is order[2] = D1.
        assert_eq!(picks(&mut t, 1)[0].0, "D1");
        let updates: Value = serde_json::from_str(&t.config_updates()).unwrap();
        assert_eq!(updates["_schedule_seq_pos"], 3);
        assert_eq!(updates["_schedule_expect_center"], false);
        assert_eq!(updates["_schedule_peripheral_pos"], 1);
    }

    #[test]
    fn center_out_explicit_peripherals_excludes_unlisted() {
        let (c, raw) = sched_cfg(
            r#"{"mode": "center_out", "center": "C", "peripherals": ["U1", "D1"],
                "peripheral_order": "sequential"}"#,
            "",
        );
        let mut t = Trial::new(c, &raw, 0.0, 42);
        let got = picks(&mut t, 8);
        // R1 is enabled but not listed: it must never appear in a peripheral slot.
        let expect = ["C", "U1", "C", "D1", "C", "U1", "C", "D1"];
        for (i, (name, phase)) in got.iter().enumerate() {
            assert_eq!(name, expect[i], "pick {i}");
            assert_eq!(phase, if i % 2 == 0 { "center" } else { "peripheral" }, "pick {i}");
        }
    }

    #[test]
    fn trial_info_json_reports_phase_name_mode() {
        let (c, raw) = sched_cfg(
            r#"{"mode": "center_out", "center": "C"}"#,
            r#", "trial_timeout": 0.5, "ignore_idle_trial_failures": true"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.1, 0.0, 0.0); // -> start_on (center slot)
        let info: Value = serde_json::from_str(&t.trial_info_json()).unwrap();
        assert_eq!(info["schedule_phase"], "center");
        assert_eq!(info["target_name"], "C");
        assert_eq!(info["mode"], "center_out");
        // Idle timeout re-arms the intertrial; the next start_on is a peripheral.
        step_at(&mut t, 0.7, 0.0, 0.0);
        let out = step_at(&mut t, 0.85, 0.0, 0.0);
        assert!(out.effects.contains(&Effect::LogState("start_on")));
        let info: Value = serde_json::from_str(&t.trial_info_json()).unwrap();
        assert_eq!(info["schedule_phase"], "peripheral");
        assert_eq!(info["target_name"], "U1");
    }

    #[test]
    fn center_out_alternates_across_trials_via_config_merge() {
        // Full cross-trial simulation of the delegate loop: each trial is a
        // fresh Trial (new run_trial stream); config_updates from trial N are
        // merged into task_config before trial N+1, exactly like
        // joystick_intro_rust.py applies config_updates_json.
        let base = r#"{"intertrial_interval": 0.1, "trial_timeout": 10.0,
            "control_mode": "direct", "targets": [
                {"name": "C",  "x_norm": 0.5, "y_norm": 0.5},
                {"name": "U1", "x_norm": 0.5, "y_norm": 0.9},
                {"name": "R1", "x_norm": 0.9, "y_norm": 0.5}
            ], "target_schedule": {"mode": "center_out", "center": "C"}}"#;
        let mut task_config: Value = serde_json::from_str(base).unwrap();
        let mut shown = Vec::new();
        for trial in 0..6 {
            let json = serde_json::to_string(&task_config).unwrap();
            let cfg = TaskConfig::from_json(&json).unwrap();
            let raw: Value = serde_json::from_str(&json).unwrap();
            let mut t = Trial::new(cfg, &raw, trial as f64 * 100.0, 7 + trial);
            t.begin();
            step_at(&mut t, trial as f64 * 100.0 + 0.1, 0.0, 0.0); // -> start_on
            shown.push((t.current_target.name.clone(), t.current_target.schedule_phase.clone()));
            // Merge config updates like the delegate does.
            let updates: Value = serde_json::from_str(&t.config_updates()).unwrap();
            for (key, value) in updates.as_object().unwrap() {
                task_config[key] = value.clone();
            }
        }
        let expect = [
            ("C", "center"),
            ("U1", "peripheral"),
            ("C", "center"),
            ("R1", "peripheral"),
            ("C", "center"),
            ("U1", "peripheral"),
        ];
        for (i, (name, phase)) in shown.iter().enumerate() {
            assert_eq!((name.as_str(), phase.as_str()), expect[i], "trial {i}");
        }
    }

    #[test]
    fn schedule_phase_lands_in_attempt_and_target_on_event() {
        let (c, raw) = sched_cfg(r#"{"mode": "sequence", "order": ["R1"]}"#, "");
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        step_at(&mut t, 0.1, 0.0, 0.0); // -> start_on
        let a = t.current_attempt.as_ref().unwrap();
        assert_eq!(a.schedule_phase.as_deref(), Some("sequence"));
        let target_on = &a.events[0];
        assert_eq!(target_on.name, "target_on");
        assert_eq!(target_on.extra["schedule_phase"], "sequence");
        assert_eq!(t.current_target.name, "R1");
    }

    #[test]
    fn require_center_gates_iti() {
        let (c, raw) = cfg(
            r#"{"require_center_before_trial": true, "center_gate_radius_ratio": 0.15,
                "intertrial_interval": 0.1, "control_mode": "direct",
                "direct_recenter_when_idle": false, "reset_cursor_each_trial": false,
                "_last_cursor_x": 0.95, "_last_cursor_y": 0.95}"#,
        );
        let mut t = Trial::new(c, &raw, 0.0, 7);
        t.begin();
        // Cursor far from center: gate never opens.
        step_at(&mut t, 1.0, 0.0, 0.0);
        step_at(&mut t, 2.0, 0.0, 0.0);
        assert_eq!(t.state, State::Intertrial);
        // Drive the cursor to center (direct mode, active stick at 0).. use
        // operator arrows? Simplest: recentering via direct active sample ~0
        // is below threshold; use a tiny active sample toward center.
        // Direct with analog (0.0001..) is below motion floor -> latched? No:
        // not latched; recenter_when_idle false so cursor stays. Move actively:
        // Active sample (hypot 0.042 >= zero_drift_buffer 0.03) recenters the
        // cursor near (0.487, 0.487) — inside the center gate. This same step
        // arms the gate: iti_end = 2.5 + 0.1.
        step_at(&mut t, 2.5, -0.03, -0.03);
        assert_eq!(t.state, State::Intertrial);
        let out = step_at(&mut t, 2.65, -0.03, -0.03);
        assert!(out.effects.contains(&Effect::LogState("start_on")), "gate should open after centered ITI");
    }
}
