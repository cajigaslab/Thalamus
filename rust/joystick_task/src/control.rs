//! RustTask gRPC service — the delegate <-> Rust control channel.
//!
//! The Python delegate (joystick_intro_rust.py) is the client; we host this
//! service. Per trial it opens run_trial as a bidirectional stream: the first
//! request carries TrialConfig, later requests forward operator inputs (arrow
//! keys, free-play end, touch). We stream back BehavState markers, the final
//! behav_result JSON, config updates, and the terminal success flag.
//!
//! Threading: this service lives on a background tokio runtime. The trial
//! itself executes on the render thread (render::Executor) at the display
//! rate; this module builds all the plumbing (input pump, effects task,
//! operator pump), assembles a render::Job, and wakes the winit loop.

use std::pin::Pin;
use std::sync::{Arc, Mutex};

use serde_json::Value;
use tokio::sync::mpsc;
use tokio_stream::{wrappers::UnboundedReceiverStream, Stream, StreamExt};
use tonic::{Request, Response, Status, Streaming};
use winit::event_loop::EventLoopProxy;

use crate::clock::ClockMap;
use crate::config::TaskConfig;
use crate::grpc::ThalamusConn;
use crate::input::JoystickState;
use crate::proto::rust_task::{
    operator_event, rust_task_server::RustTask, trial_request, FrameRequest, MirrorFrame,
    TrialEvent, TrialRequest,
};
use crate::proto::thalamus::AnalogResponse;
use crate::render::{ship_effect, EffectMsg, Job, OpEvent, Wake};
use crate::state::Trial;

pub struct RustTaskService {
    /// One long-lived connection to the core, shared by EVERY trial. Its log and
    /// inject_analog streams are opened once and reused for the life of the
    /// process, so the core keeps exactly one handler thread for each. The old
    /// per-trial `ThalamusConn::connect` leaked a TCP connection + a parked
    /// inject_analog server thread every trial (~7/min), climbing VIRT and
    /// pinning the core's io_context until the machine fell over.
    pub conn: Arc<tokio::sync::Mutex<ThalamusConn>>,
    /// Hands assembled trials to the render thread.
    pub job_tx: Mutex<std::sync::mpsc::Sender<Job>>,
    /// Wakes the winit loop when a job is queued.
    pub wake: Mutex<EventLoopProxy<Wake>>,
    /// Latest frame's shape list, published by the render thread.
    pub mirror_rx: tokio::sync::watch::Receiver<crate::mirror::MirrorScene>,
}

type TrialEventStream = Pin<Box<dyn Stream<Item = Result<TrialEvent, Status>> + Send>>;
type MirrorFrameStream = Pin<Box<dyn Stream<Item = Result<MirrorFrame, Status>> + Send>>;

/// Port of analog_processor (joystick_intro.py:2299-2332): back-date each
/// sample from the receive time by the span's sample interval, log every
/// sample, keep the last as the live joystick value.
async fn pump_input(
    mut stream: Streaming<AnalogResponse>,
    clock: ClockMap,
    joystick: JoystickState,
    sample_tx: std::sync::mpsc::Sender<(f64, f64, f64)>,
) {
    while let Some(item) = stream.next().await {
        let msg = match item {
            Ok(m) => m,
            Err(status) => {
                tracing::warn!(%status, "joystick analog stream error");
                break;
            }
        };
        let received_s = clock.now_python_ns() as f64 / 1e9;
        if msg.spans.len() >= 2 {
            let xs = crate::input::span_slice(&msg.data, &msg.spans[0]);
            let ys = crate::input::span_slice(&msg.data, &msg.spans[1]);
            let n = xs.len().min(ys.len());
            if n == 0 {
                continue;
            }
            let interval_of = |i: usize| {
                msg.sample_intervals
                    .get(i)
                    .map(|v| (*v as f64 / 1e9).max(0.0))
                    .unwrap_or(0.0)
            };
            let x_int = interval_of(0);
            let y_int = interval_of(1);
            let interval = if x_int > 0.0 { x_int } else { y_int };
            let start = received_s - interval * (n - 1) as f64;
            let mut last = (0.0, 0.0);
            for i in 0..n {
                let t = if interval > 0.0 {
                    start + interval * i as f64
                } else {
                    received_s
                };
                last = (xs[i], ys[i]);
                if sample_tx.send((t, xs[i], ys[i])).is_err() {
                    return; // trial ended
                }
            }
            joystick.set(last);
        } else if msg.data.len() >= 2 {
            let (x, y) = (msg.data[0], msg.data[1]);
            if sample_tx.send((received_s, x, y)).is_err() {
                return;
            }
            joystick.set((x, y));
        }
    }
}

/// Consume EffectMsg requests: BehavState logs and reward pulses. Owns the
/// ThalamusConn for the trial. Reward repeats are 50 ms apart
/// (deliver_reward_repeats, joystick_intro.py:2367-2371).
async fn run_effects(
    conn: Arc<tokio::sync::Mutex<ThalamusConn>>,
    mut effect_rx: mpsc::UnboundedReceiver<EffectMsg>,
    reward_ms: Vec<f64>,
    reward_scale: f64,
) {
    while let Some(msg) = effect_rx.recv().await {
        match msg {
            EffectMsg::Log { text, time_ns } => {
                // Lock only for the enqueue onto the shared log stream.
                let mut conn = conn.lock().await;
                if let Err(e) = conn.log(text, time_ns).await {
                    tracing::warn!("BehavState log failed: {e:#}");
                }
            }
            EffectMsg::Reward { channel, repeats } => {
                for i in 0..repeats.max(0) {
                    // Hold the lock only for each inject, NOT across the 50 ms
                    // gap — otherwise an overlapping trial's logs/rewards stall.
                    {
                        let mut conn = conn.lock().await;
                        if let Err(e) = crate::reward::deliver_reward(
                            &mut conn,
                            &reward_ms,
                            channel as i32,
                            reward_scale,
                        )
                        .await
                        {
                            tracing::warn!("reward delivery failed: {e:#}");
                        }
                    }
                    if i < repeats - 1 {
                        tokio::time::sleep(std::time::Duration::from_millis(50)).await;
                    }
                }
            }
        }
    }
}

#[tonic::async_trait]
impl RustTask for RustTaskService {
    type run_trialStream = TrialEventStream;
    type framesStream = MirrorFrameStream;

    async fn run_trial(
        &self,
        request: Request<Streaming<TrialRequest>>,
    ) -> Result<Response<Self::run_trialStream>, Status> {
        let mut inbound = request.into_inner();

        let first = inbound
            .next()
            .await
            .ok_or_else(|| Status::invalid_argument("empty run_trial request stream"))??;
        let Some(trial_request::Body::Config(cfg)) = first.body else {
            return Err(Status::invalid_argument(
                "first run_trial message must carry TrialConfig",
            ));
        };

        // Seed the Python perf_counter mapping as early as possible.
        let clock = ClockMap::seed(cfg.python_perf_ns);
        let task_config = TaskConfig::from_json(&cfg.config_json).map_err(|e| {
            // Surface this loudly: a client-side send race can mask the
            // status as INTERNAL, making the executor log the only witness.
            tracing::error!("rejected config_json: {e}");
            Status::invalid_argument(format!("bad config_json: {e}"))
        })?;
        let raw: Value = serde_json::from_str(&cfg.config_json)
            .map_err(|e| Status::invalid_argument(format!("bad config_json: {e}")))?;

        tracing::info!(
            joystick_node = %task_config.joystick_node,
            cursor_only = task_config.cursor_only_mode,
            reward_scale = cfg.reward_scale,
            "run_trial received"
        );

        // Input pump: analog stream -> latest cell + per-sample log channel.
        // `input_conn` is a fresh STREAM handle over the process-wide shared
        // channel (no new TCP connection). The analog SUBSCRIBE takes ~1 s on
        // the C++ core to send headers, so it must NOT block trial start — hence
        // the spawn; Python never awaits it either. When the trial ends the pump
        // returns and this handle drops, tearing down only the analog stream —
        // the shared channel (and the log/inject streams) stays up.
        let joystick = JoystickState::default();
        let (sample_tx, sample_rx) = std::sync::mpsc::channel();
        let mut input_conn = self.conn.lock().await.clone_conn();
        let node = task_config.joystick_node.clone();
        let input_clock = clock;
        let input_joystick = joystick.clone();
        tokio::spawn(async move {
            match input_conn.analog_xy(&node).await {
                Ok(stream) => pump_input(stream, input_clock, input_joystick, sample_tx).await,
                Err(e) => tracing::warn!("joystick stream open failed: {e:#}"),
            }
        });

        // Effects task: BehavState logs + reward pulses go through the SHARED
        // conn, so their streams are opened once and reused every trial.
        let (effect_tx, effect_rx) = mpsc::unbounded_channel();
        tokio::spawn(run_effects(
            self.conn.clone(),
            effect_rx,
            cfg.reward_ms.clone(),
            cfg.reward_scale,
        ));

        // Operator-event pump: remaining request messages -> render thread.
        let (op_tx, op_rx) = std::sync::mpsc::channel();
        tokio::spawn(async move {
            while let Some(item) = inbound.next().await {
                let req = match item {
                    Ok(r) => r,
                    Err(_) => break, // client hung up; the render loop notices via event_tx
                };
                let Some(trial_request::Body::Operator(op)) = req.body else {
                    continue;
                };
                let mapped = match op.body {
                    Some(operator_event::Body::Arrow(a)) => OpEvent::Arrow {
                        key: a.key,
                        pressed: a.pressed,
                    },
                    Some(operator_event::Body::EndRequested(v)) => OpEvent::EndRequested(v),
                    Some(operator_event::Body::Touch(t)) => OpEvent::Touch { x: t.x, y: t.y },
                    None => continue,
                };
                if op_tx.send(mapped).is_err() {
                    break;
                }
            }
        });

        // Build the trial and emit its initial BehavState=intertrial.
        let now_s = clock.now_python_ns() as f64 / 1e9;
        let mut trial = Trial::new(task_config, &raw, now_s, cfg.python_perf_ns | 1);
        let (event_tx, event_rx) = mpsc::unbounded_channel();
        for effect in trial.begin() {
            ship_effect(&clock, &effect_tx, &event_tx, effect);
        }

        let job = Job {
            trial,
            clock,
            joystick,
            sample_rx,
            op_rx,
            event_tx,
            effect_tx,
        };
        self.job_tx
            .lock()
            .unwrap()
            .send(job)
            .map_err(|_| Status::internal("render thread is gone"))?;
        let _ = self.wake.lock().unwrap().send_event(Wake::Job);

        Ok(Response::new(
            Box::pin(UnboundedReceiverStream::new(event_rx)) as TrialEventStream,
        ))
    }

    async fn frames(
        &self,
        request: Request<FrameRequest>,
    ) -> Result<Response<Self::framesStream>, Status> {
        let req = request.into_inner();
        let hz = if req.max_hz == 0 { 30 } else { req.max_hz.clamp(1, 60) };
        let max_width = if req.max_width == 0 { 480 } else { req.max_width.clamp(64, 1920) };
        let mut mirror_rx = self.mirror_rx.clone();
        tracing::info!(hz, max_width, "operator mirror stream opened");

        let (tx, rx) = mpsc::channel::<Result<MirrorFrame, Status>>(2);
        tokio::spawn(async move {
            let mut ticker =
                tokio::time::interval(std::time::Duration::from_micros(1_000_000 / u64::from(hz)));
            ticker.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
            loop {
                ticker.tick().await;
                let scene = mirror_rx.borrow_and_update().clone();
                let encoded = tokio::task::spawn_blocking(move || {
                    crate::mirror::encode_frame(&scene, max_width)
                })
                .await;
                let frame = match encoded {
                    Ok(Ok((jpeg, width, height))) => MirrorFrame {
                        jpeg,
                        width,
                        height,
                        time_ns: crate::clock::monotonic_now_ns(),
                    },
                    Ok(Err(e)) => {
                        tracing::warn!("mirror encode failed: {e:#}");
                        continue;
                    }
                    Err(join_err) => {
                        tracing::warn!("mirror encode task failed: {join_err}");
                        continue;
                    }
                };
                // send() blocks if the operator is slow; the bounded queue plus
                // Skip tick behavior naturally drops frames instead of lagging.
                if tx.send(Ok(frame)).await.is_err() {
                    tracing::info!("operator mirror stream closed");
                    break;
                }
            }
        });
        Ok(Response::new(Box::pin(
            tokio_stream::wrappers::ReceiverStream::new(rx),
        ) as MirrorFrameStream))
    }
}
