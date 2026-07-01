//! Reward delivery via `inject_analog`.
//!
//! Byte-faithful port of `deliver_reward` (joystick_intro.py:2342-2365):
//!   base_ms      = context.get_reward(channel)      # resolved by Python, passed in TrialConfig.reward_ms
//!   on_time_ms   = round(base_ms * reward_scale)
//!   if on_time_ms <= 0: skip
//!   signal = AnalogResponse(data=[5, 0],
//!                           spans=[Span(begin=0, end=2, name="Reward")],
//!                           sample_intervals=[1_000_000 * on_time_ms])
//!   inject_analog("Reward", signal)
//!
//! IMPORTANT: Rust does NOT compute the reward schedule. Python owns it
//! (task_context.py:726-728) and hands us the resolved per-channel `base_ms`.

use crate::grpc::ThalamusConn;
use crate::proto::thalamus::{AnalogResponse, Span};

/// Node name the reward is injected into (matches joystick_intro.py:2365).
pub const REWARD_NODE: &str = "Reward";

/// Build the reward pulse AnalogResponse for a given on-time in milliseconds.
/// Returns None if the duration is non-positive (Python logs and skips).
pub fn build_reward_signal(on_time_ms: i64) -> Option<AnalogResponse> {
    if on_time_ms <= 0 {
        return None;
    }
    Some(AnalogResponse {
        data: vec![5.0, 0.0],
        spans: vec![Span {
            begin: 0,
            end: 2,
            name: "Reward".to_string(),
            scale: 0.0,
            offset: 0.0,
        }],
        sample_intervals: vec![1_000_000u64 * on_time_ms as u64],
        ..Default::default()
    })
}

/// Compute on_time_ms exactly like Python: round(base_ms * reward_scale), with
/// reward_scale clamped to [0, 100] and defaulting to 1.0 (joystick_intro.py:2348).
pub fn on_time_ms(base_ms: f64, reward_scale: f64) -> i64 {
    let scale = if reward_scale.is_finite() {
        reward_scale.clamp(0.0, 100.0)
    } else {
        1.0
    };
    (base_ms * scale).round() as i64
}

/// Deliver one reward pulse on `channel`. `reward_ms` is the resolved per-channel
/// base durations from TrialConfig; `reward_scale` from the task config.
pub async fn deliver_reward(
    conn: &mut ThalamusConn,
    reward_ms: &[f64],
    channel: i32,
    reward_scale: f64,
) -> anyhow::Result<()> {
    let base_ms = reward_ms.get(channel.max(0) as usize).copied().unwrap_or(0.0);
    let ms = on_time_ms(base_ms, reward_scale);
    match build_reward_signal(ms) {
        Some(signal) => {
            tracing::info!(channel, base_ms, reward_scale, duration_ms = ms, "delivering reward");
            conn.inject_signal(REWARD_NODE, signal).await
        }
        None => {
            tracing::info!(channel, base_ms, reward_scale, "reward skipped (<= 0 ms)");
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn on_time_rounds_and_clamps() {
        assert_eq!(on_time_ms(100.0, 1.0), 100);
        assert_eq!(on_time_ms(100.0, 1.5), 150);
        assert_eq!(on_time_ms(100.0, -1.0), 0); // scale clamped to 0
        assert_eq!(on_time_ms(33.3, 1.0), 33); // round-half handled by f64::round
    }

    #[test]
    fn signal_shape_matches_python() {
        let s = build_reward_signal(250).unwrap();
        assert_eq!(s.data, vec![5.0, 0.0]);
        assert_eq!(s.spans.len(), 1);
        assert_eq!(s.spans[0].begin, 0);
        assert_eq!(s.spans[0].end, 2);
        assert_eq!(s.spans[0].name, "Reward");
        assert_eq!(s.sample_intervals, vec![1_000_000 * 250]);
    }

    #[test]
    fn zero_duration_yields_no_signal() {
        assert!(build_reward_signal(0).is_none());
    }
}
