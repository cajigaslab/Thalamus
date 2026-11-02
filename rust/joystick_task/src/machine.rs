//! The seam between the render loop and a task's state machine.
//!
//! One executor process hosts several task types (joystick_intro,
//! simple_touch). control.rs picks the machine from config_json's task_type,
//! boxes it into the render::Job, and render::Executor drives it through this
//! trait only — it never needs to know which task is running.
//!
//! A Rust "trait" is an interface: any type that implements these methods can
//! be stored as `Box<dyn TrialMachine>` and called without knowing its
//! concrete type (dynamic dispatch, like a Python duck-typed object).

use crate::render::{OpEvent, SceneUniforms};
pub use crate::state::{Effect, StepInput, StepOutput};

/// One input sample from the per-trial input pump (control.rs), already in
/// the Python perf_counter domain (seconds).
#[derive(Debug, Clone, Copy)]
pub enum InputSample {
    /// Joystick analog (x, y) — every sample lands in behav_result.
    Joystick { t_s: f64, x: f64, y: f64 },
    /// Touch in subject-window pixels; (-1, -1) means "no touch" (release),
    /// exactly what Canvas.on_ros_touch hands touch_listener.
    Touch { x: i64, y: i64 },
}

pub trait TrialMachine: Send {
    /// Effects to ship before the first frame (the initial BehavState).
    fn begin(&mut self) -> Vec<Effect>;
    /// Operator input forwarded by the delegate.
    fn on_op(&mut self, op: OpEvent, now_s: f64);
    /// One input sample, delivered in arrival order before step().
    fn on_sample(&mut self, sample: InputSample);
    /// Advance one display frame.
    fn step(&mut self, input: &StepInput) -> StepOutput;
    /// This frame's subject-display scene.
    fn scene(&self, w: i64, h: i64) -> SceneUniforms;
    /// Final behav_result (key-order faithful JSON).
    fn behav_result_json(&self) -> anyhow::Result<String>;
    /// Cross-trial bookkeeping for the delegate to write back into task_config.
    fn config_updates_json(&self) -> String;
    /// Operator-only metadata, sent to the delegate's HUD whenever the machine
    /// logs BehavState=start_on (never logged, never on the subject display).
    fn trial_info_json(&self) -> Option<String>;
    /// Photodiode square for the idle scene after this trial ends.
    fn idle_indicator(&self) -> crate::render::IdleIndicator;
}

/// joystick_intro: a thin wrapper over the existing state::Trial API, so the
/// joystick behavior is byte-for-byte what it was before the trait existed.
impl TrialMachine for crate::state::Trial {
    fn begin(&mut self) -> Vec<Effect> {
        crate::state::Trial::begin(self)
    }

    fn on_op(&mut self, op: OpEvent, now_s: f64) {
        match op {
            OpEvent::Arrow { key, pressed } => self.on_arrow_key(&key, pressed),
            OpEvent::EndRequested(v) => self.on_end_requested(v),
            OpEvent::Touch { x, y } => self.on_touch(x as i64, y as i64, now_s),
        }
    }

    fn on_sample(&mut self, sample: InputSample) {
        // Touch never reaches this machine through the pump (joystick_intro
        // gets touch from the delegate as an OpEvent), so only joystick here.
        if let InputSample::Joystick { t_s, x, y } = sample {
            self.on_joystick_sample(t_s, x, y);
        }
    }

    fn step(&mut self, input: &StepInput) -> StepOutput {
        crate::state::Trial::step(self, input)
    }

    fn scene(&self, w: i64, h: i64) -> SceneUniforms {
        crate::render::build_scene(self, w, h)
    }

    fn behav_result_json(&self) -> anyhow::Result<String> {
        self.behav.to_json()
    }

    fn config_updates_json(&self) -> String {
        self.config_updates()
    }

    fn trial_info_json(&self) -> Option<String> {
        Some(crate::state::Trial::trial_info_json(self))
    }

    fn idle_indicator(&self) -> crate::render::IdleIndicator {
        crate::render::IdleIndicator::FromBottomRight(
            self.cfg.state_indicator_x.max(0),
            self.cfg.state_indicator_y.max(0),
        )
    }
}
