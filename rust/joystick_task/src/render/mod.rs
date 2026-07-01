//! Subject-display rendering (M1 latency-floor spike, then M3 full scene).
//!
//! STATUS: stub. The render stack (winit + wgpu) is commented out in Cargo.toml
//! to keep the foundation build light and cross-platform; it only builds cleanly
//! on the Linux rig. Uncomment those deps when starting M1.
//!
//! Design (see docs/rust_bci_patch.md "Rendering stack"):
//!   - winit window, EXCLUSIVE / unredirected fullscreen on the 240 Hz output so
//!     the compositor is bypassed (~1 frame instead of ~2).
//!   - wgpu surface, PresentMode::Fifo for the production photodiode-faithful path;
//!     expose Immediate/Mailbox behind a flag to measure the tear-allowed floor.
//!   - FRAME-PACED loop, input sampled LAST before submit:
//!       loop each vsync:
//!         (x, y) = joystick.get()            // freshest sample
//!         outcome = trial.step(dt)           // advance state machine
//!         scene.record(&trial)               // rects/ellipse/ring/cursor/HUD
//!         queue.submit(); surface.present()  // present at vsync
//!   - Photodiode square drawn LAST, full-coverage, in the same screen corner and
//!     size as joystick_intro.py:2828-2836 (state_width=70, offset by
//!     state_indicator_x/y). It is the latency measurement instrument — keep it
//!     pixel/timing faithful.

use crate::state::{Trial, TrialOutcome};

/// Present mode selection (maps to wgpu::PresentMode once render deps are on).
#[derive(Debug, Clone, Copy)]
pub enum PresentMode {
    /// Hard vsync, no tearing (~1 frame). Production / photodiode-faithful.
    Fifo,
    /// Lowest latency, tearing allowed. For measuring the floor.
    Immediate,
    /// Triple-buffered low latency, no tearing.
    Mailbox,
}

#[derive(Debug, Clone)]
pub struct RenderConfig {
    pub fullscreen: bool,
    pub present_mode: PresentMode,
    /// Monitor index for the subject display on a multi-head rig.
    pub monitor: usize,
}

impl Default for RenderConfig {
    fn default() -> Self {
        Self {
            fullscreen: true,
            present_mode: PresentMode::Fifo,
            monitor: 0,
        }
    }
}

/// Run the frame-paced render + state loop until the trial terminates, returning
/// the outcome. TODO(M1/M3): implement with winit + wgpu.
pub fn run_loop(_trial: &mut Trial, _cfg: &RenderConfig) -> TrialOutcome {
    unimplemented!("M1: winit+wgpu window & frame pacing; M3: full scene from trial state")
}
