//! Photodiode recording + command-to-photon analysis for the M1 spike.
//!
//! On this rig the photodiode feeds the NIDAQ node ("Analog in", Dev1/ai1,
//! 1 kHz) exposed through the "Node 5" channel picker as channel "Photodiode".
//! The NIDAQ node stamps each poll batch with C++ steady_clock at read time
//! (nidaq_node_linux.cpp:242) and the gRPC layer forwards it as
//! `AnalogResponse.time` (grpc_impl.cpp:353). steady_clock == CLOCK_MONOTONIC
//! on Linux, i.e. the SAME clock as the spike's `t_cmd_ns` — so sample times
//! and toggle-command times are directly comparable, no cross-clock sync step.
//!
//! Per-sample times back-date from the batch stamp by the per-span
//! `sample_interval`: the last sample in a batch was read ~now, earlier ones
//! 1 ms (at 1 kHz) apart. Accuracy is therefore ~1-2 ms — fine for deciding
//! whether the render path costs 5 or 50 ms.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use tokio_stream::StreamExt;

use crate::grpc::ThalamusConn;
use crate::proto::thalamus::AnalogResponse;

/// One diode sample in CLOCK_MONOTONIC ns.
pub type Sample = (u64, f64);

/// Streams the photodiode channel on a background thread while the render loop
/// owns the main thread. `stop()` joins and returns everything captured.
pub struct DiodeRecorder {
    handle: std::thread::JoinHandle<anyhow::Result<Vec<Sample>>>,
    stop: Arc<AtomicBool>,
}

impl DiodeRecorder {
    pub fn start(endpoint: String, node: String, channel: String) -> Self {
        let stop = Arc::new(AtomicBool::new(false));
        let stop_flag = stop.clone();
        let handle = std::thread::Builder::new()
            .name("diode-recorder".into())
            .spawn(move || {
                tokio::runtime::Builder::new_current_thread()
                    .enable_all()
                    .build()?
                    .block_on(record(endpoint, node, channel, stop_flag))
            })
            .expect("spawning diode-recorder thread");
        Self { handle, stop }
    }

    /// Signal the reader to finish and return the captured samples.
    pub fn stop(self) -> anyhow::Result<Vec<Sample>> {
        self.stop.store(true, Ordering::Relaxed);
        match self.handle.join() {
            Ok(res) => res,
            Err(_) => anyhow::bail!("diode-recorder thread panicked"),
        }
    }
}

async fn record(
    endpoint: String,
    node: String,
    channel: String,
    stop: Arc<AtomicBool>,
) -> anyhow::Result<Vec<Sample>> {
    let connect = ThalamusConn::connect(endpoint.clone());
    let mut conn = tokio::time::timeout(std::time::Duration::from_secs(3), connect)
        .await
        .map_err(|_| anyhow::anyhow!("timed out connecting to Thalamus at {endpoint}"))??;
    let mut stream = conn.analog_channels(&node, &[&channel]).await?;
    tracing::info!(%node, %channel, "photodiode stream open");

    let mut samples: Vec<Sample> = Vec::with_capacity(64 * 1024);
    loop {
        // Poll with a timeout so the stop flag is honored even if the node is
        // not producing data (e.g. NIDAQ node not Running).
        match tokio::time::timeout(std::time::Duration::from_millis(200), stream.next()).await {
            Ok(Some(Ok(msg))) => append_channel_samples(&msg, &channel, &mut samples),
            Ok(Some(Err(status))) => anyhow::bail!("photodiode stream error: {status}"),
            Ok(None) => break, // server closed the stream
            Err(_elapsed) => {} // no data this tick; check stop flag below
        }
        if stop.load(Ordering::Relaxed) {
            break;
        }
    }
    Ok(samples)
}

/// Extract this channel's samples from one AnalogResponse, timestamping each by
/// back-dating from the batch time (see module docs).
fn append_channel_samples(msg: &AnalogResponse, channel: &str, out: &mut Vec<Sample>) {
    // With a single-channel subscription there is one span; match by name when
    // the server sends more.
    let idx = if msg.spans.len() == 1 {
        0
    } else {
        match msg.spans.iter().position(|s| s.name == channel) {
            Some(i) => i,
            None => return,
        }
    };
    let Some(span) = msg.spans.get(idx) else { return };
    let b = (span.begin as usize).min(msg.data.len());
    let e = (span.end as usize).min(msg.data.len());
    if e <= b {
        return;
    }
    let data = &msg.data[b..e];
    let n = data.len() as u64;
    let interval = msg.sample_intervals.get(idx).copied().unwrap_or(0);
    for (i, v) in data.iter().enumerate() {
        let back = (n - 1 - i as u64) * interval;
        out.push((msg.time.saturating_sub(back), *v));
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Edge {
    pub t_ns: u64,
    pub rising: bool,
}

/// Threshold-with-hysteresis edge detector. Returns detected edges with the
/// edge time linearly interpolated to the midpoint crossing between the two
/// bracketing samples, plus the (vmin, vmax) swing seen.
pub fn detect_edges(samples: &[Sample]) -> (Vec<Edge>, f64, f64) {
    let (mut vmin, mut vmax) = (f64::INFINITY, f64::NEG_INFINITY);
    for &(_, v) in samples {
        vmin = vmin.min(v);
        vmax = vmax.max(v);
    }
    let mut edges = Vec::new();
    let range = vmax - vmin;
    if !range.is_finite() || range <= 0.0 {
        return (edges, vmin, vmax);
    }
    let mid = vmin + range / 2.0;
    let hys = 0.15 * range;
    // true = high state
    let mut state = samples[0].1 > mid;
    let mut prev = samples[0];
    for &(t, v) in &samples[1..] {
        let flipped = if state { v < mid - hys } else { v > mid + hys };
        if flipped {
            // Interpolate the mid crossing between prev and this sample.
            let (t0, v0) = (prev.0 as f64, prev.1);
            let (t1, v1) = (t as f64, v);
            let frac = if (v1 - v0).abs() > f64::EPSILON {
                ((mid - v0) / (v1 - v0)).clamp(0.0, 1.0)
            } else {
                0.5
            };
            edges.push(Edge {
                t_ns: (t0 + frac * (t1 - t0)) as u64,
                rising: !state,
            });
            state = !state;
        }
        prev = (t, v);
    }
    (edges, vmin, vmax)
}

/// One matched toggle -> photon measurement.
#[derive(Debug, Clone, Copy)]
pub struct Latency {
    pub toggle_idx: u64,
    pub t_cmd_ns: u64,
    pub t_photon_ns: u64,
    pub latency_ms: f64,
    pub white: bool,
}

/// Match each toggle command to the first diode edge of the expected direction
/// within one toggle period. `white_is_rising` sets diode polarity.
pub fn match_toggles(
    toggles: &[(u64, u64, bool)], // (toggle_idx, t_cmd_ns, white)
    edges: &[Edge],
    period_ns: u64,
    white_is_rising: bool,
) -> Vec<Latency> {
    let window = (period_ns as f64 * 0.9) as u64;
    let mut out = Vec::new();
    for &(idx, t_cmd, white) in toggles {
        let want_rising = white == white_is_rising;
        // Edges are time-ordered; binary search for the first edge after t_cmd.
        let start = edges.partition_point(|e| e.t_ns <= t_cmd);
        if let Some(e) = edges[start..]
            .iter()
            .take_while(|e| e.t_ns - t_cmd < window)
            .find(|e| e.rising == want_rising)
        {
            out.push(Latency {
                toggle_idx: idx,
                t_cmd_ns: t_cmd,
                t_photon_ns: e.t_ns,
                latency_ms: (e.t_ns - t_cmd) as f64 / 1e6,
                white,
            });
        }
    }
    out
}

fn percentile(sorted_ms: &[f64], p: f64) -> f64 {
    sorted_ms[((sorted_ms.len() - 1) as f64 * p) as usize]
}

/// Full analysis: edges -> polarity auto-detect -> matching -> printed report.
/// Returns the per-toggle latencies (empty when the measurement failed).
pub fn analyze_and_report(samples: &[Sample], toggles: &[(u64, u64, bool)]) -> Vec<Latency> {
    if samples.len() < 100 {
        println!(
            "diode: only {} samples captured — is the NIDAQ node Running?",
            samples.len()
        );
        return vec![];
    }
    if toggles.len() < 5 {
        println!("diode: too few toggles ({}) to analyze", toggles.len());
        return vec![];
    }

    let (edges, vmin, vmax) = detect_edges(samples);
    let dur_s = (samples.last().unwrap().0 - samples[0].0) as f64 / 1e9;
    println!(
        "diode: {} samples over {:.1}s, swing {:.3} -> {:.3} V, {} edges",
        samples.len(), dur_s, vmin, vmax, edges.len()
    );
    if vmax - vmin < 0.05 {
        println!("diode: swing < 0.05 V — diode probably not seeing the square; aborting analysis");
        return vec![];
    }

    // Median toggle period from the command timestamps.
    let mut periods: Vec<u64> = toggles.windows(2).map(|w| w[1].1 - w[0].1).collect();
    periods.sort_unstable();
    let period_ns = periods[periods.len() / 2];
    if period_ns < 40_000_000 {
        println!(
            "diode: toggle period {:.1} ms is too short for the 1 kHz diode channel — \
             rerun with a larger --toggle-frames (aim for a period >= 50 ms)",
            period_ns as f64 / 1e6
        );
        return vec![];
    }

    // Skip warmup toggles (window creation / first presents are not steady-state).
    let steady = &toggles[3.min(toggles.len() - 1)..];

    // Polarity auto-detect: try both, keep whichever matches more toggles.
    let normal = match_toggles(steady, &edges, period_ns, true);
    let inverted = match_toggles(steady, &edges, period_ns, false);
    let (lat, polarity) = if normal.len() >= inverted.len() {
        (normal, "white=rising")
    } else {
        (inverted, "white=falling (inverted diode)")
    };

    if lat.len() < steady.len() / 2 {
        println!(
            "diode: matched only {}/{} toggles — signal too noisy or wrong channel",
            lat.len(), steady.len()
        );
        return lat;
    }

    let mut ms: Vec<f64> = lat.iter().map(|l| l.latency_ms).collect();
    ms.sort_by(|a, b| a.partial_cmp(b).unwrap());
    println!("--- command-to-photon ({} of {} toggles matched, {polarity}) ---",
        lat.len(), steady.len());
    println!(
        "latency ms: min={:.2} p50={:.2} p90={:.2} p99={:.2} max={:.2}",
        ms[0], percentile(&ms, 0.50), percentile(&ms, 0.90), percentile(&ms, 0.99),
        ms[ms.len() - 1]
    );
    for white in [true, false] {
        let mut sub: Vec<f64> = lat.iter().filter(|l| l.white == white)
            .map(|l| l.latency_ms).collect();
        if sub.len() > 2 {
            sub.sort_by(|a, b| a.partial_cmp(b).unwrap());
            println!(
                "  {}: p50={:.2} ms (n={})",
                if white { "black->white" } else { "white->black" },
                percentile(&sub, 0.50), sub.len()
            );
        }
    }
    lat
}

#[cfg(test)]
mod tests {
    use super::*;

    const MS: u64 = 1_000_000;

    /// Synthetic 1 kHz square wave: toggles every 100 ms, photon lags command
    /// by 7 ms, diode swings 0.2 <-> 3.0 V.
    fn synth() -> (Vec<Sample>, Vec<(u64, u64, bool)>) {
        let mut samples = Vec::new();
        let mut toggles = Vec::new();
        for i in 0..3000u64 {
            let t = 1_000_000_000 + i * MS; // 3 s of 1 kHz samples
            // Command toggles at t = 1e9 + k*100ms (k even -> white); the
            // display goes photon 7 ms after each command.
            let k = (t - 1_000_000_000) / (100 * MS);
            let phase = (t - 1_000_000_000) % (100 * MS);
            let white_displayed = if phase >= 7 * MS {
                k % 2 == 0 // this period's command has reached the screen
            } else {
                k > 0 && (k - 1) % 2 == 0 // still showing the previous period
            };
            samples.push((t, if white_displayed { 3.0 } else { 0.2 }));
            if phase == 0 {
                toggles.push((k, t, k % 2 == 0));
            }
        }
        (samples, toggles)
    }

    #[test]
    fn detects_edges_with_interpolation() {
        let (samples, _) = synth();
        let (edges, vmin, vmax) = detect_edges(&samples);
        assert!(vmin < 0.5 && vmax > 2.5);
        assert!(edges.len() >= 25, "got {} edges", edges.len());
        // Directions must alternate.
        for w in edges.windows(2) {
            assert_ne!(w[0].rising, w[1].rising);
        }
    }

    #[test]
    fn matches_toggles_and_recovers_latency() {
        let (samples, toggles) = synth();
        let (edges, _, _) = detect_edges(&samples);
        let lat = match_toggles(&toggles[1..], &edges, 100 * MS, true);
        assert!(lat.len() >= 25, "matched {}", lat.len());
        for l in &lat {
            assert!(
                (l.latency_ms - 7.0).abs() < 2.0,
                "latency {} not ~7ms", l.latency_ms
            );
        }
    }

    #[test]
    fn empty_on_flat_signal() {
        let samples: Vec<Sample> = (0..1000u64).map(|i| (i * MS, 1.0)).collect();
        let (edges, _, _) = detect_edges(&samples);
        assert!(edges.is_empty());
    }
}
