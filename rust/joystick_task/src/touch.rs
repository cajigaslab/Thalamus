//! simple_touch state machine — port of simple_touch_task.run
//! (thalamus/task_controller/simple_touch_task.py:191-399), the spec of record.
//!
//! Like state.rs, the machine is pure: step() consumes one frame and returns
//! effects (BehavState logs, reward) plus the terminal outcome; all I/O lives
//! in control.rs / render. Every random draw (timeouts, target radius/angle,
//! which target is presented) is made by the Python delegate
//! (simple_touch_rust.py) through context.get_value & co., so trial_summ's
//! used_values match the Python task exactly; we receive the realized values
//! as `ResolvedTouchConfig`.
//!
//! Python-parity notes:
//!   - Timestamps are Python perf_counter seconds (see clock.rs).
//!   - Geometry replicates int() truncation and QRect::center() integer math.
//!   - wait_for (util.py:351-359) resolves when its condition fires OR the
//!     timeout expires, then returns the condition's CURRENT value. Touch
//!     events arrive between frames, so a condition that fired on an event is
//!     latched until the next step() re-evaluates it — same as Python's
//!     context.until() waking on process() and then calling condition().
//!   - INTENTIONAL DIVERGENCE (operator request): the photodiode square is
//!     placed like joystick_intro's — state_indicator_x/y are margins from
//!     the bottom-right corner — not at Python's absolute top-left (x, y).
//!     Eccentricities are measured from a configurable task center
//!     (center_x/center_y, fractions of the screen; Python always uses the
//!     screen center = 0.5/0.5).
//!   - INTENTIONAL DIVERGENCE: Python's fail_trial (@356-362) assigns
//!     show_presented_target / state_brightness without `nonlocal`, so on a
//!     fail the target stays up for the whole fail_timeout and the photodiode
//!     does not change. Here a fail hides the target and drops the photodiode
//!     square to 0, as the code clearly intended.

use serde::Deserialize;
use serde_json::{Map, Value};

use crate::constants::STATE_INDICATOR_EDGE_PX;
use crate::machine::{Effect, InputSample, StepInput, StepOutput, TrialMachine};
use crate::render::{srgb_u8, IdleIndicator, OpEvent, SceneBuilder, SceneUniforms};
use crate::state::TrialOutcome;

/// Viewing distance in ecc_to_px (simple_touch_task.py:148).
const VIEWING_DISTANCE_M: f64 = 0.4;

/// One target's realized values (get_target_value/get_target_color in Python).
#[derive(Debug, Clone, Deserialize)]
pub struct ResolvedTarget {
    pub width: f64,
    pub height: f64,
    pub radius: f64,
    pub angle: f64,
    pub window_size: f64,
    pub color: [i64; 3],
    pub reward_channel: f64,
    /// Layout-editor enable flag. A disabled target is never presented, never
    /// drawn, and its window counts as blank space. Absent = enabled (configs
    /// authored before the editor existed).
    #[serde(default = "enabled_by_default")]
    pub enabled: bool,
}

fn enabled_by_default() -> bool {
    true
}

fn screen_middle() -> f64 {
    0.5
}

/// Everything simple_touch_task.run samples up front (@202-245, @265-266).
#[derive(Debug, Clone, Deserialize)]
pub struct ResolvedTouchConfig {
    pub intertrial_timeout: f64,
    pub start_timeout: f64,
    pub hold_timeout: f64,
    pub blink_timeout: f64,
    pub fail_timeout: f64,
    pub success_timeout: f64,
    pub is_choice: bool,
    pub no_start_fail: bool,
    /// Photodiode square margins from the right / bottom screen edges (as in
    /// joystick_intro).
    pub state_indicator_x: i64,
    pub state_indicator_y: i64,
    /// Task center (the 0-eccentricity point) as fractions of the screen
    /// width / height, measured from the top-left. 0.5/0.5 = screen center.
    #[serde(default = "screen_middle")]
    pub center_x: f64,
    #[serde(default = "screen_middle")]
    pub center_y: f64,
    /// context.config['dpi'] or the canvas logicalDpiX().
    pub dpi: f64,
    /// i_presented_targ (np.random.randint over the enabled targets), indexing
    /// the FULL target list so ids match the config.
    pub presented_index: usize,
    pub targets: Vec<ResolvedTarget>,
}

impl ResolvedTouchConfig {
    pub fn from_json(json: &str) -> anyhow::Result<Self> {
        let cfg: Self = serde_json::from_str(json)?;
        if cfg.targets.is_empty() {
            anyhow::bail!("simple_touch needs at least one target");
        }
        if cfg.presented_index >= cfg.targets.len() {
            anyhow::bail!(
                "presented_index {} out of range ({} targets)",
                cfg.presented_index,
                cfg.targets.len()
            );
        }
        if !cfg.targets[cfg.presented_index].enabled {
            anyhow::bail!("presented_index {} is a disabled target", cfg.presented_index);
        }
        Ok(cfg)
    }
}

/// ecc_to_px (@144-152): degrees of eccentricity -> pixels.
pub fn ecc_to_px(ecc: f64, dpi: f64) -> f64 {
    let x_m = VIEWING_DISTANCE_M * ecc.to_radians().tan();
    x_m / 0.0254 * dpi
}

/// Integer rectangle with Qt's QRect semantics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IRect {
    pub x: i64,
    pub y: i64,
    pub w: i64,
    pub h: i64,
}

impl IRect {
    /// QRect::center(): right = x + w - 1, center = (left + right) / 2 with
    /// C++ integer division (truncates toward zero, as does Rust's `/`).
    pub fn center(&self) -> (i64, i64) {
        let right = self.x + self.w - 1;
        let bottom = self.y + self.h - 1;
        ((self.x + right) / 2, (self.y + bottom) / 2)
    }
}

/// One target's rect (get_target_rectangles @154-180) in a w x h window, with
/// the task center at (center_x * w, center_y * h).
pub fn target_rect(t: &ResolvedTarget, dpi: f64, w: i64, h: i64, center_x: f64, center_y: f64) -> IRect {
    let (sin, cos) = t.angle.to_radians().sin_cos();
    let (x_ecc, y_ecc) = (t.radius * cos, t.radius * sin);
    let width_px = ecc_to_px(t.width, dpi);
    let height_px = ecc_to_px(t.height, dpi);
    // p_win = [1, -1] * pos_vis + center  (y axis flipped); Python's center
    // is [w/2, h/2].
    let px = ecc_to_px(x_ecc, dpi) + w as f64 * center_x;
    let py = -ecc_to_px(y_ecc, dpi) + h as f64 * center_y;
    // Python int() truncates toward zero; so does `as i64`.
    IRect {
        x: (px - width_px / 2.0) as i64,
        y: (py - height_px / 2.0) as i64,
        w: width_px as i64,
        h: height_px as i64,
    }
}

fn toggle_brightness(b: i64) -> i64 {
    if b == 255 {
        0
    } else {
        255
    }
}

/// Which wait_for the machine is blocked in. `latched` records that the
/// condition fired on a touch event since the wait began.
#[derive(Debug, Clone, Copy, PartialEq)]
enum Phase {
    /// await context.sleep(intertrial_timeout)
    Intertrial { until: f64 },
    /// wait_for(acquired or blank_space_touched, start_timeout)
    StartOn { until: f64, latched: bool },
    /// wait_for_hold: wait_for(not acquired, remaining)
    Hold { hold_start: f64, until: f64, latched: bool },
    /// wait_for_hold: wait_for(acquired, blink_timeout) after a blink
    Blink { hold_start: f64, until: f64, latched: bool },
    /// await context.sleep(success_timeout / fail_timeout), then return.
    Finishing { until: f64, success: bool },
}

#[derive(Debug, Clone)]
struct Geometry {
    rects: Vec<IRect>,
    centers: Vec<(i64, i64)>,
    windows: Vec<f64>,
    /// Subject window size the geometry was computed for.
    size: (i64, i64),
}

pub struct TouchTrial {
    cfg: ResolvedTouchConfig,
    phase: Phase,
    /// Computed on the first step (needs the window size).
    geometry: Option<Geometry>,
    /// Touch events not yet applied (only before geometry exists).
    pending_touch: Vec<(i64, i64)>,
    // touch_handler state (@254-287)
    blank_space_touched: bool,
    presented_targ_acquired: bool,
    last_selected_target: Option<usize>,
    // renderer state
    show_presented_target: bool,
    state_brightness: i64,
    /// behav_result (@342-346, @374): presented_target_ids, then
    /// selected_target_id once the start is acquired.
    selected_target_id: Option<Option<usize>>,
}

impl TouchTrial {
    pub fn new(cfg: ResolvedTouchConfig, now_s: f64) -> Self {
        let until = now_s + cfg.intertrial_timeout;
        Self {
            cfg,
            phase: Phase::Intertrial { until },
            geometry: None,
            pending_touch: Vec::new(),
            blank_space_touched: false,
            presented_targ_acquired: false,
            last_selected_target: None,
            show_presented_target: false,
            state_brightness: 0,
            selected_target_id: None,
        }
    }

    fn presented_ids(&self) -> Vec<usize> {
        if self.cfg.is_choice {
            (0..self.cfg.targets.len()).filter(|&i| self.cfg.targets[i].enabled).collect()
        } else {
            vec![self.cfg.presented_index]
        }
    }

    /// touch_handler (@254-287), one event.
    fn apply_touch(&mut self, x: i64, y: i64) {
        let Some(g) = self.geometry.as_ref() else {
            self.pending_touch.push((x, y));
            return;
        };
        let targets = &self.cfg.targets;
        let in_window = |i: usize| {
            if !targets[i].enabled {
                return false;
            }
            let (cx, cy) = g.centers[i];
            let (dx, dy) = ((cx - x) as f64, (cy - y) as f64);
            (dx * dx + dy * dy).sqrt() < g.windows[i]
        };
        let in_windows: Vec<bool> = (0..g.rects.len()).map(in_window).collect();
        let any_in = in_windows.iter().any(|v| *v);
        self.blank_space_touched = self.blank_space_touched || (!any_in && x >= 0);

        let selected = if self.cfg.is_choice {
            in_windows.iter().position(|v| *v)
        } else {
            let i = self.cfg.presented_index;
            in_windows[i].then_some(i)
        };
        self.presented_targ_acquired = selected.is_some();
        if selected.is_some() {
            self.last_selected_target = selected;
        }
        self.latch_wait();
    }

    /// The condition of the wait_for currently blocking, evaluated now.
    fn wait_condition(&self) -> bool {
        match self.phase {
            Phase::StartOn { .. } => self.presented_targ_acquired || self.blank_space_touched,
            Phase::Hold { .. } => !self.presented_targ_acquired,
            Phase::Blink { .. } => self.presented_targ_acquired,
            Phase::Intertrial { .. } | Phase::Finishing { .. } => false,
        }
    }

    fn latch_wait(&mut self) {
        if self.wait_condition() {
            match &mut self.phase {
                Phase::StartOn { latched, .. }
                | Phase::Hold { latched, .. }
                | Phase::Blink { latched, .. } => *latched = true,
                _ => {}
            }
        }
    }

    fn ensure_geometry(&mut self, w: i64, h: i64) {
        if self.geometry.is_some() {
            return;
        }
        let dpi = self.cfg.dpi;
        let (cx, cy) = (self.cfg.center_x, self.cfg.center_y);
        let rects: Vec<IRect> =
            self.cfg.targets.iter().map(|t| target_rect(t, dpi, w, h, cx, cy)).collect();
        let centers = rects.iter().map(IRect::center).collect();
        let windows = self.cfg.targets.iter().map(|t| ecc_to_px(t.window_size, dpi)).collect();
        tracing::debug!(
            width = w,
            height = h,
            dpi,
            centers = ?centers,
            windows_px = ?windows,
            "simple_touch target geometry (window-local px; compare with touch local_x/y)"
        );
        self.geometry = Some(Geometry { rects, centers, windows, size: (w, h) });
        for (x, y) in std::mem::take(&mut self.pending_touch) {
            self.apply_touch(x, y);
        }
    }

    /// fail_trial (@356-362) + the fail_timeout sleep. See the module docs
    /// for the intentional show/brightness fix.
    fn fail(&mut self, now: f64, out: &mut StepOutput) {
        out.effects.push(Effect::LogState("fail"));
        self.show_presented_target = false;
        self.state_brightness = 0;
        self.phase = Phase::Finishing { until: now + self.cfg.fail_timeout, success: false };
    }

    /// wait_for_hold loop head (util.py:465-475): remaining time is measured
    /// from the hold start, blink time NOT added back.
    fn hold_or_succeed(&mut self, hold_start: f64, now: f64, out: &mut StepOutput) {
        let remaining = self.cfg.hold_timeout - (now - hold_start);
        if remaining < 0.0 {
            self.succeed(now, out);
        } else {
            self.phase = Phase::Hold { hold_start, until: now + remaining, latched: false };
        }
    }

    /// Success branch (@377-399).
    fn succeed(&mut self, now: f64, out: &mut StepOutput) {
        self.show_presented_target = false;
        out.effects.push(Effect::LogState("success"));
        self.state_brightness = toggle_brightness(self.state_brightness);
        // final_i_selected_target is always set once the start is acquired.
        let selected = self.last_selected_target.unwrap_or(self.cfg.presented_index);
        // get_reward(int(channel)): the config stores channels as floats.
        let channel = self.cfg.targets[selected].reward_channel as i64;
        out.effects.push(Effect::Reward { channel, repeats: 1 });
        self.phase = Phase::Finishing { until: now + self.cfg.success_timeout, success: true };
    }
}

impl TrialMachine for TouchTrial {
    /// First loop iteration's BehavState=intertrial (@365).
    fn begin(&mut self) -> Vec<Effect> {
        vec![Effect::LogState("intertrial")]
    }

    /// Touch comes from the touch pump (InputSample), not the delegate; the
    /// delegate forwards no operator inputs for this task.
    fn on_op(&mut self, _op: OpEvent, _now_s: f64) {}

    fn on_sample(&mut self, sample: InputSample) {
        if let InputSample::Touch { x, y, .. } = sample {
            self.apply_touch(x, y);
        }
    }

    fn step(&mut self, inp: &StepInput) -> StepOutput {
        let mut out = StepOutput::default();
        let now = inp.now_s;
        self.ensure_geometry(inp.width_px, inp.height_px);

        match self.phase {
            Phase::Intertrial { until } => {
                if now >= until {
                    // @370-375
                    self.blank_space_touched = false;
                    out.effects.push(Effect::LogState("start_on"));
                    self.state_brightness = toggle_brightness(self.state_brightness);
                    self.show_presented_target = true;
                    self.phase = Phase::StartOn { until: now + self.cfg.start_timeout, latched: false };
                    // A finger already resting on the target satisfies the
                    // condition immediately (context.until checks it first).
                    self.latch_wait();
                }
            }
            Phase::StartOn { until, latched } => {
                let cond = self.wait_condition();
                if latched || cond || now >= until {
                    // `acquired and not blank_space_touched` (@378), where
                    // acquired is wait_for's re-evaluated condition.
                    if cond && !self.blank_space_touched {
                        // state: startacq (@392-395)
                        self.selected_target_id = Some(self.last_selected_target);
                        out.effects.push(Effect::LogState("start_acq"));
                        self.phase = Phase::Hold {
                            hold_start: now,
                            until: now + self.cfg.hold_timeout,
                            latched: false,
                        };
                        self.latch_wait();
                    } else if self.cfg.no_start_fail {
                        self.fail(now, &mut out);
                    } else {
                        // Hide and go round the loop again (@387-389), re-using
                        // this trial's sampled intertrial interval.
                        self.show_presented_target = false;
                        out.effects.push(Effect::LogState("intertrial"));
                        self.state_brightness = 0;
                        self.phase = Phase::Intertrial { until: now + self.cfg.intertrial_timeout };
                    }
                }
            }
            Phase::Hold { hold_start, until, latched } => {
                let cond = self.wait_condition();
                if latched || cond || now >= until {
                    if cond {
                        // blinked (util.py:477-480)
                        out.effects.push(Effect::LogState("blink"));
                        self.phase = Phase::Blink {
                            hold_start,
                            until: now + self.cfg.blink_timeout,
                            latched: false,
                        };
                    } else {
                        // Held through the timeout — or released and
                        // re-touched between frames, where wait_for also
                        // returns False — so `if not blinked: break` -> success.
                        self.succeed(now, &mut out);
                    }
                }
            }
            Phase::Blink { hold_start, until, latched } => {
                let cond = self.wait_condition();
                if latched || cond || now >= until {
                    if cond {
                        self.hold_or_succeed(hold_start, now, &mut out);
                    } else {
                        self.fail(now, &mut out);
                    }
                }
            }
            Phase::Finishing { until, success } => {
                if now >= until {
                    out.done = Some(TrialOutcome { success });
                }
            }
        }
        out
    }

    fn scene(&self, w: i64, h: i64) -> SceneUniforms {
        let mut s = SceneBuilder::new();
        // Targets (@296-309): fillRect only — `shape` is ignored by Python too.
        if self.show_presented_target {
            if let Some(g) = &self.geometry {
                let ids = self.presented_ids();
                for i in ids {
                    let r = g.rects[i];
                    s.filled_rect(
                        r.x as f32,
                        r.y as f32,
                        (r.x + r.w) as f32,
                        (r.y + r.h) as f32,
                        srgb_u8(self.cfg.targets[i].color, 1.0),
                    );
                }
            }
        }
        // Photodiode square, bottom-right anchored like joystick_intro
        // (render::build_scene) — see the module docs.
        let edge = STATE_INDICATOR_EDGE_PX as f32;
        let sx = w as f32 - edge - self.cfg.state_indicator_x as f32;
        let sy = h as f32 - edge - self.cfg.state_indicator_y as f32;
        let b = self.state_brightness;
        s.filled_rect(sx, sy, sx + edge, sy + edge, srgb_u8([b, b, b], 1.0));
        s.uniforms
    }

    /// behav_result in Python's dict order; selected_target_id is absent on a
    /// no-start fail, exactly like the Python dict.
    fn behav_result_json(&self) -> anyhow::Result<String> {
        let mut m = Map::new();
        m.insert(
            "presented_target_ids".into(),
            Value::from(self.presented_ids()),
        );
        if let Some(selected) = self.selected_target_id {
            m.insert("selected_target_id".into(), selected.map_or(Value::Null, Value::from));
        }
        Ok(serde_json::to_string(&m)?)
    }

    fn config_updates_json(&self) -> String {
        "{}".to_string()
    }

    /// Operator HUD overlay: centers + acceptance windows of the enabled
    /// targets, in subject pixels (Python draws these OPERATOR-masked,
    /// @311-317).
    fn trial_info_json(&self) -> Option<String> {
        let g = self.geometry.as_ref()?;
        let targets: Vec<Value> = (0..self.cfg.targets.len())
            .filter(|&i| self.cfg.targets[i].enabled)
            .map(|i| serde_json::json!({ "center": g.centers[i], "window": g.windows[i] }))
            .collect();
        Some(
            serde_json::json!({
                "task": "simple_touch",
                "presented": self.presented_ids(),
                "targets": targets,
                "subject_width": g.size.0,
                "subject_height": g.size.1,
            })
            .to_string(),
        )
    }

    fn idle_indicator(&self) -> IdleIndicator {
        IdleIndicator::FromBottomRight(self.cfg.state_indicator_x.max(0), self.cfg.state_indicator_y.max(0))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: i64 = 1920;
    const H: i64 = 1080;

    fn target(radius: f64, angle: f64) -> ResolvedTarget {
        ResolvedTarget {
            width: 3.0,
            height: 3.0,
            radius,
            angle,
            window_size: 8.0,
            color: [138, 226, 52],
            reward_channel: 3.0,
            enabled: true,
        }
    }

    fn cfg(targets: Vec<ResolvedTarget>) -> ResolvedTouchConfig {
        ResolvedTouchConfig {
            intertrial_timeout: 1.0,
            start_timeout: 2.0,
            hold_timeout: 0.5,
            blink_timeout: 0.2,
            fail_timeout: 1.0,
            success_timeout: 1.0,
            is_choice: false,
            no_start_fail: true,
            state_indicator_x: 30,
            state_indicator_y: 70,
            center_x: 0.5,
            center_y: 0.5,
            dpi: 96.0,
            presented_index: 0,
            targets,
        }
    }

    fn input(now_s: f64) -> StepInput {
        StepInput {
            now_s,
            analog_x: 0.0,
            analog_y: 0.0,
            width_px: W,
            height_px: H,
            input_stale: false,
        }
    }

    fn touch(t: &mut TouchTrial, x: i64, y: i64) {
        t.on_sample(InputSample::Touch { x, y });
    }

    fn logs(out: &StepOutput) -> Vec<&'static str> {
        out.effects
            .iter()
            .filter_map(|e| match e {
                Effect::LogState(s) => Some(*s),
                _ => None,
            })
            .collect()
    }

    /// Values computed with the Python get_target_rectangles + QRect.center()
    /// for dpi 96 in a 1920x1080 canvas.
    #[test]
    fn geometry_matches_python() {
        // Reference values from simple_touch_task.py's ecc_to_px /
        // get_target_rectangles run with numpy + PyQt6 QRect.center() for the
        // eevee.json targets (3 deg box, radius 0/10) at dpi 96, 1920x1080.
        assert!((ecc_to_px(3.0, 96.0) - 79.230_658_443_652_86).abs() < 1e-9);
        assert!((ecc_to_px(8.0, 96.0) - 212.471_183_172_119_4).abs() < 1e-9);
        let cases = [
            ((0.0, 0.0), (920, 500), (959, 539)),
            ((10.0, 0.0), (1186, 500), (1225, 539)),
            ((10.0, 45.0), (1107, 312), (1146, 351)),
            ((10.0, 90.0), (920, 233), (959, 272)),
            ((10.0, 135.0), (732, 312), (771, 351)),
            ((10.0, 180.0), (653, 500), (692, 539)),
            ((10.0, 225.0), (732, 687), (771, 726)),
            ((10.0, 315.0), (1107, 687), (1146, 726)),
        ];
        for ((radius, angle), (x, y), center) in cases {
            let r = target_rect(&target(radius, angle), 96.0, W, H, 0.5, 0.5);
            assert_eq!(r, IRect { x, y, w: 79, h: 79 }, "radius {radius} angle {angle}");
            assert_eq!(r.center(), center, "radius {radius} angle {angle}");
        }
    }

    /// The exact shape simple_touch_rust.py::_resolve sends (values from an
    /// eevee.json run), so a field rename on either side fails here.
    #[test]
    fn parses_delegate_resolved_json() {
        let json = r#"{"intertrial_timeout": 0.93, "start_timeout": 2.1,
            "hold_timeout": 0.195, "blink_timeout": 0.5, "fail_timeout": 9.8,
            "success_timeout": 1.0, "is_choice": false, "no_start_fail": true,
            "state_indicator_x": 1800, "state_indicator_y": 950, "dpi": 96.0,
            "presented_index": 1, "targets": [
              {"width": 3.0, "height": 3.0, "radius": 0.0, "angle": 0.0,
               "window_size": 8.0, "color": [138, 226, 52], "reward_channel": 3.0},
              {"width": 3.0, "height": 3.0, "radius": 10.0, "angle": 45.0,
               "window_size": 8.0, "color": [138, 226, 52], "reward_channel": 3.0}]}"#;
        let c = ResolvedTouchConfig::from_json(json).unwrap();
        assert_eq!(c.presented_index, 1);
        assert_eq!(c.targets[1].color, [138, 226, 52]);
        let bad = json.replace(r#""presented_index": 1"#, r#""presented_index": 2"#);
        assert!(ResolvedTouchConfig::from_json(&bad).is_err());
    }

    #[test]
    fn task_center_shifts_every_target() {
        // Center raised to 40% of the height: 0.1 * 1080 = 108 px higher.
        let base = target_rect(&target(10.0, 45.0), 96.0, W, H, 0.5, 0.5);
        let raised = target_rect(&target(10.0, 45.0), 96.0, W, H, 0.5, 0.4);
        assert_eq!((raised.x, raised.y), (base.x, base.y - 108));
    }

    #[test]
    fn photodiode_is_bottom_right_anchored() {
        let mut t = TouchTrial::new(cfg(vec![target(0.0, 0.0)]), 0.0);
        t.step(&input(0.0));
        let u = t.scene(W, H);
        let square = u.shapes[u.shape_count() - 1];
        // 1920 - 70 - 30, 1080 - 70 - 70
        assert_eq!(&square.geo, &[1820.0, 940.0, 1890.0, 1010.0]);
    }

    #[test]
    fn center_truncates_toward_zero_like_cpp() {
        let r = IRect { x: -10, y: -10, w: 4, h: 4 };
        // right = -7, (-10 + -7) / 2 = -8 (C++: -8, floor would be -9)
        assert_eq!(r.center(), (-8, -8));
    }

    #[test]
    fn success_path_logs_and_rewards() {
        let mut t = TouchTrial::new(cfg(vec![target(0.0, 0.0)]), 0.0);
        assert_eq!(t.begin(), vec![Effect::LogState("intertrial")]);
        assert!(logs(&t.step(&input(0.5))).is_empty());
        assert_eq!(logs(&t.step(&input(1.0))), vec!["start_on"]);
        assert_eq!(t.state_brightness, 255);
        touch(&mut t, 960, 540);
        assert_eq!(logs(&t.step(&input(1.1))), vec!["start_acq"]);
        assert!(logs(&t.step(&input(1.4))).is_empty());
        let out = t.step(&input(1.61));
        assert_eq!(logs(&out), vec!["success"]);
        assert!(out.effects.contains(&Effect::Reward { channel: 3, repeats: 1 }));
        assert_eq!(t.state_brightness, 0);
        assert!(!t.show_presented_target);
        assert!(t.step(&input(2.0)).done.is_none());
        assert!(t.step(&input(2.62)).done.unwrap().success);
        assert_eq!(
            t.behav_result_json().unwrap(),
            r#"{"presented_target_ids":[0],"selected_target_id":0}"#
        );
    }

    #[test]
    fn blank_space_touch_fails_with_no_start_fail() {
        let mut t = TouchTrial::new(cfg(vec![target(0.0, 0.0)]), 0.0);
        t.step(&input(1.0));
        touch(&mut t, 10, 10);
        let out = t.step(&input(1.05));
        assert_eq!(logs(&out), vec!["fail"]);
        // Intentional fix: target hidden, photodiode dark at fail onset.
        assert!(!t.show_presented_target);
        assert_eq!(t.state_brightness, 0);
        assert!(!t.step(&input(2.06)).done.unwrap().success);
        // No start acquired -> selected_target_id absent, like Python.
        assert_eq!(t.behav_result_json().unwrap(), r#"{"presented_target_ids":[0]}"#);
    }

    #[test]
    fn start_timeout_loops_without_no_start_fail() {
        let mut c = cfg(vec![target(0.0, 0.0)]);
        c.no_start_fail = false;
        let mut t = TouchTrial::new(c, 0.0);
        t.step(&input(1.0));
        let out = t.step(&input(3.0));
        assert_eq!(logs(&out), vec!["intertrial"]);
        assert!(!t.show_presented_target);
        assert_eq!(logs(&t.step(&input(4.0))), vec!["start_on"]);
    }

    #[test]
    fn touch_during_intertrial_does_not_count_as_blank() {
        let mut t = TouchTrial::new(cfg(vec![target(0.0, 0.0)]), 0.0);
        t.step(&input(0.1));
        touch(&mut t, 10, 10);
        touch(&mut t, -1, -1);
        assert_eq!(logs(&t.step(&input(1.0))), vec!["start_on"]);
        assert!(logs(&t.step(&input(1.1))).is_empty());
    }

    #[test]
    fn blink_then_reacquire_still_succeeds_on_time() {
        let mut t = TouchTrial::new(cfg(vec![target(0.0, 0.0)]), 0.0);
        t.step(&input(1.0));
        touch(&mut t, 960, 540);
        t.step(&input(1.0)); // start_acq, hold_start = 1.0
        touch(&mut t, -1, -1);
        assert_eq!(logs(&t.step(&input(1.2))), vec!["blink"]);
        touch(&mut t, 960, 540);
        assert!(logs(&t.step(&input(1.3))).is_empty()); // back to Hold, 0.2 s left
        // Blink time is NOT added back: hold ends at 1.5.
        assert_eq!(logs(&t.step(&input(1.51))), vec!["success"]);
    }

    #[test]
    fn blink_too_long_fails() {
        let mut t = TouchTrial::new(cfg(vec![target(0.0, 0.0)]), 0.0);
        t.step(&input(1.0));
        touch(&mut t, 960, 540);
        t.step(&input(1.0));
        touch(&mut t, -1, -1);
        t.step(&input(1.1));
        assert_eq!(logs(&t.step(&input(1.31))), vec!["fail"]);
        assert_eq!(
            t.behav_result_json().unwrap(),
            r#"{"presented_target_ids":[0],"selected_target_id":0}"#
        );
    }

    #[test]
    fn choice_picks_first_window_hit_and_its_reward_channel() {
        let mut far = target(10.0, 0.0);
        far.reward_channel = 5.0;
        let mut c = cfg(vec![target(0.0, 0.0), far]);
        c.is_choice = true;
        let mut t = TouchTrial::new(c, 0.0);
        t.step(&input(1.0));
        let g = t.geometry.clone().unwrap();
        let (x, y) = g.centers[1];
        touch(&mut t, x, y);
        t.step(&input(1.1));
        let out = t.step(&input(1.7));
        assert!(out.effects.contains(&Effect::Reward { channel: 5, repeats: 1 }));
        assert_eq!(
            t.behav_result_json().unwrap(),
            r#"{"presented_target_ids":[0,1],"selected_target_id":1}"#
        );
    }

    #[test]
    fn disabled_targets_are_skipped_and_their_window_is_blank_space() {
        let mut off = target(10.0, 0.0);
        off.enabled = false;
        let mut c = cfg(vec![target(0.0, 0.0), off, target(10.0, 180.0)]);
        c.is_choice = true;
        let mut t = TouchTrial::new(c, 0.0);
        t.step(&input(1.0));
        let g = t.geometry.clone().unwrap();
        assert_eq!(t.presented_ids(), vec![0, 2]);
        let (x, y) = g.centers[1];
        touch(&mut t, x, y);
        assert!(!t.presented_targ_acquired);
        assert!(t.blank_space_touched);
        assert_eq!(logs(&t.step(&input(1.05))), vec!["fail"]);
        assert_eq!(t.behav_result_json().unwrap(), r#"{"presented_target_ids":[0,2]}"#);
    }

    #[test]
    fn presenting_a_disabled_target_is_rejected() {
        let json = r#"{"intertrial_timeout": 1, "start_timeout": 1, "hold_timeout": 1,
            "blink_timeout": 1, "fail_timeout": 1, "success_timeout": 1,
            "is_choice": false, "no_start_fail": true, "state_indicator_x": 0,
            "state_indicator_y": 0, "dpi": 96, "presented_index": 0, "targets": [
              {"width": 3, "height": 3, "radius": 0, "angle": 0, "window_size": 8,
               "color": [1, 2, 3], "reward_channel": 3, "enabled": false}]}"#;
        assert!(ResolvedTouchConfig::from_json(json).is_err());
    }

    #[test]
    fn touches_before_first_frame_are_applied_in_order() {
        let mut t = TouchTrial::new(cfg(vec![target(0.0, 0.0)]), 0.0);
        touch(&mut t, 960, 540);
        touch(&mut t, -1, -1);
        t.step(&input(0.1));
        assert!(!t.presented_targ_acquired);
        assert!(!t.blank_space_touched);
    }
}
