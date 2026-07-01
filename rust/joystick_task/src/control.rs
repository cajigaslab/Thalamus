//! RustTask gRPC service — the delegate <-> Rust control channel.
//!
//! The Python delegate (joystick_intro_rust.py) is the client; we host this
//! service. Per trial it calls run_trial(TrialConfig) and consumes the TrialEvent
//! stream (BehavState markers, behav_result JSON, terminal success).
//!
//! STATUS: server scaffold. Config parsing + clock seeding are wired; the actual
//! trial execution (connect analog input, run the render/state loop, emit events)
//! is the M3/M4 integration point and currently returns Unimplemented cleanly.

use std::pin::Pin;

use tokio::sync::mpsc;
use tokio_stream::{wrappers::ReceiverStream, Stream};
use tonic::{Request, Response, Status};

use crate::clock::ClockMap;
use crate::config::TaskConfig;
use crate::proto::rust_task::{
    rust_task_server::RustTask, FrameRequest, MirrorFrame, TrialConfig, TrialEvent,
};

/// Endpoint of the Thalamus core we forward input/log/reward to.
#[derive(Clone)]
pub struct RustTaskService {
    pub thalamus_endpoint: String,
}

impl RustTaskService {
    pub fn new(thalamus_endpoint: String) -> Self {
        Self { thalamus_endpoint }
    }
}

type TrialEventStream = Pin<Box<dyn Stream<Item = Result<TrialEvent, Status>> + Send>>;
type MirrorFrameStream = Pin<Box<dyn Stream<Item = Result<MirrorFrame, Status>> + Send>>;

#[tonic::async_trait]
impl RustTask for RustTaskService {
    type run_trialStream = TrialEventStream;
    type framesStream = MirrorFrameStream;

    async fn run_trial(
        &self,
        request: Request<TrialConfig>,
    ) -> Result<Response<Self::run_trialStream>, Status> {
        let cfg = request.into_inner();

        // Parse the operator config (same JSON as the Python task_config).
        let task_config = TaskConfig::from_json(&cfg.config_json)
            .map_err(|e| Status::invalid_argument(format!("bad config_json: {e}")))?;
        // Seed the Python perf_counter clock mapping for byte-parity logging.
        let _clock = ClockMap::seed(cfg.python_perf_ns);

        tracing::info!(
            joystick_node = %task_config.joystick_node,
            reward_scale = cfg.reward_scale,
            "run_trial received"
        );

        // TODO(M3/M4): wire the real pipeline here —
        //   1. grpc::ThalamusConn::connect(self.thalamus_endpoint)
        //   2. conn.analog_xy(&task_config.joystick_node) -> input::spawn_reader
        //   3. state::Trial::new(task_config, clock, joystick)
        //   4. render::run_loop(&mut trial, &RenderConfig::default())
        //   5. stream TrialEvent::Marker per transition, then Success.
        // For now, return a stream that reports the not-yet-implemented state.
        let (tx, rx) = mpsc::channel::<Result<TrialEvent, Status>>(1);
        tokio::spawn(async move {
            let _ = tx
                .send(Err(Status::unimplemented(
                    "trial execution lands in M3/M4 (see src/state.rs, src/render/mod.rs)",
                )))
                .await;
        });
        Ok(Response::new(Box::pin(ReceiverStream::new(rx))))
    }

    async fn frames(
        &self,
        _request: Request<FrameRequest>,
    ) -> Result<Response<Self::framesStream>, Status> {
        // TODO(M4): stream downscaled JPEG frames (mirror.rs).
        Err(Status::unimplemented("operator mirror lands in M4 (see src/mirror.rs)"))
    }
}
