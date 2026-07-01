//! Clock mapping: Rust `steady_clock` -> Python `time.perf_counter()` domain.
//!
//! Why this matters: Python logs event times as `int(time.perf_counter()*1e9)`
//! (task_context.py:409) with NO server-side sync. For our Rust-emitted
//! `BehavState=` Text records to interleave correctly with Python's TRIAL
//! START/FINISHED brackets in the capture file, our `Text.time` must land in that
//! same perf_counter domain.
//!
//! Mechanism: the delegate captures Python's perf_counter ns right before calling
//! `run_trial` and passes it as `TrialConfig.python_perf_ns`. We record our own
//! monotonic clock at receipt; the difference is a fixed offset applied to every
//! logged timestamp for the trial.
//!
//! Refinement (TODO, M3/M4): remove the one-way call latency bias with a short
//! ping/pong over the control channel (min-RTT sample), and independently use
//! Thalamus.ping + Pong.remote_time for photodiode/SyncNode alignment during
//! latency measurement.

use std::time::Instant;

#[derive(Debug, Clone, Copy)]
pub struct ClockMap {
    /// Local monotonic reference captured when the seed arrived.
    local_ref: Instant,
    /// Python perf_counter ns at (approximately) that same instant.
    python_ref_ns: u64,
}

impl ClockMap {
    /// Seed from the delegate-provided Python perf_counter ns, taken as close as
    /// possible to "now".
    pub fn seed(python_perf_ns: u64) -> Self {
        Self {
            local_ref: Instant::now(),
            python_ref_ns: python_perf_ns,
        }
    }

    /// Map "now" into the Python perf_counter ns domain.
    pub fn now_python_ns(&self) -> u64 {
        let elapsed = self.local_ref.elapsed().as_nanos() as u64;
        self.python_ref_ns.saturating_add(elapsed)
    }

    /// Map an arbitrary local `Instant` into the Python perf_counter ns domain.
    /// `at` must be >= the seed instant.
    pub fn to_python_ns(&self, at: Instant) -> u64 {
        let elapsed = at.saturating_duration_since(self.local_ref).as_nanos() as u64;
        self.python_ref_ns.saturating_add(elapsed)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn maps_forward_monotonically() {
        let m = ClockMap::seed(1_000_000_000);
        let a = m.now_python_ns();
        std::thread::sleep(Duration::from_millis(2));
        let b = m.now_python_ns();
        assert!(b >= a);
        assert!(b >= 1_000_000_000);
    }
}
