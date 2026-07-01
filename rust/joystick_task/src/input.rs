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

use std::sync::Arc;

use arc_swap::ArcSwap;
use tokio_stream::StreamExt;

use crate::proto::thalamus::AnalogResponse;

/// Latest joystick sample, lock-free. `(x, y)` in the device's native units
/// (the Python code treats them as ~[-1, 1] analog; no scaling applied here).
#[derive(Clone)]
pub struct JoystickState {
    latest: Arc<ArcSwap<(f64, f64)>>,
}

impl Default for JoystickState {
    fn default() -> Self {
        Self {
            latest: Arc::new(ArcSwap::from_pointee((0.0, 0.0))),
        }
    }
}

impl JoystickState {
    /// Read the freshest (x, y). Call once per frame.
    pub fn get(&self) -> (f64, f64) {
        *self.latest.load_full()
    }

    fn set(&self, xy: (f64, f64)) {
        self.latest.store(Arc::new(xy));
    }
}

/// Extract the latest (X, Y) from one AnalogResponse, or None if it carries no
/// usable sample. Pure function — unit-tested against the Python parsing rules.
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

/// Spawn the reader task. Returns the shared `JoystickState` the loop reads.
pub fn spawn_reader(mut stream: tonic::Streaming<AnalogResponse>) -> JoystickState {
    let state = JoystickState::default();
    let out = state.clone();
    tokio::spawn(async move {
        while let Some(item) = stream.next().await {
            match item {
                Ok(msg) => {
                    if let Some(xy) = parse_xy(&msg) {
                        state.set(xy);
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
