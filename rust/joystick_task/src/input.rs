//! Joystick input sampling.
//!
//! A dedicated tokio task drains the `analog` stream and writes the latest (X, Y)
//! into a lock-free cell. The render/state loop reads the newest value once per
//! frame and NEVER blocks on the stream — input can never stall the present.
//!
//! Wire-format parsing mirrors `analog_processor` (joystick_intro.py:2299-2330):
//!   - if >= 2 spans: spans[0]=X, spans[1]=Y; slice data[span.begin..span.end];
//!     take the LAST sample of each as the latest value.
//!   - else if data has >= 2 elements: data[0]=X, data[1]=Y.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use arc_swap::ArcSwap;
use tokio_stream::StreamExt;

use crate::proto::thalamus::AnalogResponse;

/// Latest joystick sample, lock-free. `(x, y)` in the device's native units
/// (the Python code treats them as ~[-1, 1] analog; no scaling applied here).
///
/// Safety layer (Phase 0 of the closed-loop BCI roadmap): non-finite (NaN/Inf)
/// samples are dropped before they ever reach `set()` (see `control::pump_input`),
/// and every accepted sample stamps its arrival time so `age()` lets a consumer
/// detect a stalled/hung analog source (hardware joystick OR a BCI decoder feeding
/// the "Decoder" node) instead of silently rendering a frozen last-known value
/// forever.
#[derive(Clone)]
pub struct JoystickState {
    latest: Arc<ArcSwap<(f64, f64, Instant)>>,
    faults: Arc<AtomicU64>,
}

impl Default for JoystickState {
    fn default() -> Self {
        Self {
            latest: Arc::new(ArcSwap::from_pointee((0.0, 0.0, Instant::now()))),
            faults: Arc::new(AtomicU64::new(0)),
        }
    }
}

impl JoystickState {
    /// Read the freshest (x, y). Call once per frame.
    pub fn get(&self) -> (f64, f64) {
        let (x, y, _) = *self.latest.load_full();
        (x, y)
    }

    /// Time since the last accepted sample. Growing without bound means the
    /// analog source has stopped delivering (hung decoder, dropped connection, etc).
    pub fn age(&self) -> Duration {
        let (_, _, t) = *self.latest.load_full();
        t.elapsed()
    }

    pub(crate) fn set(&self, xy: (f64, f64)) {
        self.latest.store(Arc::new((xy.0, xy.1, Instant::now())));
    }

    /// Count of non-finite (NaN/Inf) samples dropped since start. Not yet wired
    /// to any consumer (operator HUD/telemetry) -- part of the Phase 0 safety
    /// API surface for that follow-up.
    #[allow(dead_code)]
    pub fn fault_count(&self) -> u64 {
        self.faults.load(Ordering::Relaxed)
    }

    pub(crate) fn record_fault(&self) {
        self.faults.fetch_add(1, Ordering::Relaxed);
    }
}

/// Slice `data` by a span's begin/end, clamped (shared with control::pump_input).
pub(crate) fn span_slice<'a>(data: &'a [f64], span: &crate::proto::thalamus::Span) -> &'a [f64] {
    slice(data, span.begin, span.end)
}

/// Extract the latest (X, Y) from one AnalogResponse, or None if it carries no
/// usable sample. Pure function — unit-tested against the Python parsing rules.
/// (Superseded by control::pump_input for the trial path, which also logs every
/// sample; kept for the tests and simple consumers.)
#[allow(dead_code)]
pub fn parse_xy(msg: &AnalogResponse) -> Option<(f64, f64)> {
    if msg.spans.len() >= 2 {
        let xs = &msg.spans[0];
        let ys = &msg.spans[1];
        let x_slice = slice(&msg.data, xs.begin, xs.end);
        let y_slice = slice(&msg.data, ys.begin, ys.end);
        let n = x_slice.len().min(y_slice.len());
        if n == 0 {
            return None;
        }
        // Python keeps the last sample as the current value.
        Some((x_slice[n - 1], y_slice[n - 1]))
    } else if msg.data.len() >= 2 {
        Some((msg.data[0], msg.data[1]))
    } else {
        None
    }
}

fn slice(data: &[f64], begin: u32, end: u32) -> &[f64] {
    let b = (begin as usize).min(data.len());
    let e = (end as usize).min(data.len());
    if e > b {
        &data[b..e]
    } else {
        &[]
    }
}

/// Spawn a latest-only reader task (no behav_result sample logging — the trial
/// path uses control::pump_input instead).
#[allow(dead_code)]
pub fn spawn_reader(mut stream: tonic::Streaming<AnalogResponse>) -> JoystickState {
    let state = JoystickState::default();
    let out = state.clone();
    tokio::spawn(async move {
        while let Some(item) = stream.next().await {
            match item {
                Ok(msg) => {
                    if let Some(xy) = parse_xy(&msg) {
                        if xy.0.is_finite() && xy.1.is_finite() {
                            state.set(xy);
                        } else {
                            state.record_fault();
                        }
                    }
                }
                Err(status) => {
                    tracing::warn!(%status, "analog stream error; input reader exiting");
                    break;
                }
            }
        }
        tracing::info!("analog input stream ended");
    });
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::proto::thalamus::Span;

    fn span(begin: u32, end: u32) -> Span {
        Span {
            begin,
            end,
            name: String::new(),
            scale: 0.0,
            offset: 0.0,
        }
    }

    #[test]
    fn parses_two_span_last_sample() {
        let msg = AnalogResponse {
            data: vec![0.1, 0.2, 0.3, /*Y*/ 0.7, 0.8, 0.9],
            spans: vec![span(0, 3), span(3, 6)],
            ..Default::default()
        };
        assert_eq!(parse_xy(&msg), Some((0.3, 0.9)));
    }

    #[test]
    fn parses_flat_two_element_fallback() {
        let msg = AnalogResponse {
            data: vec![0.25, -0.4],
            spans: vec![],
            ..Default::default()
        };
        assert_eq!(parse_xy(&msg), Some((0.25, -0.4)));
    }

    #[test]
    fn none_when_empty_spans() {
        let msg = AnalogResponse {
            data: vec![],
            spans: vec![span(0, 0), span(0, 0)],
            ..Default::default()
        };
        assert_eq!(parse_xy(&msg), None);
    }
}
