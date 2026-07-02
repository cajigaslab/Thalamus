//! thalamus-joystick-task: low-latency Rust executor for the joystick_intro
//! behavioral task (real-time BCI patch).
//!
//! Process model: long-lived. Connects to the Thalamus core (localhost:50050)
//! as an ordinary insecure gRPC client for input/log/reward, and HOSTS the
//! RustTask control service (localhost:50060) that the Python delegate calls
//! per trial.
//!
//! Threading: the MAIN thread runs the winit/wgpu render loop (the subject
//! display owns it — see render/mod.rs for the latency rationale); the tonic
//! server runs on a background tokio runtime and hands trials to the render
//! thread through a channel + user-event wakeup.
//!
//! See rust/README.md and docs/rust_bci_patch.md for architecture, milestones,
//! and the measured latency numbers that shaped this design.

mod clock;
mod config;
mod constants;
mod control;
mod events;
mod grpc;
mod input;
mod mirror;
mod proto;
mod render;
mod reward;
mod state;

use clap::Parser;
use tonic::transport::Server;
use winit::event_loop::{ControlFlow, EventLoop};

use crate::control::RustTaskService;
use crate::proto::rust_task::rust_task_server::RustTaskServer;
use crate::render::{Executor, ExecutorArgs, Wake};

#[derive(Parser, Debug)]
#[command(name = "joystick_intro_executor", version, about)]
struct Args {
    /// Thalamus core gRPC endpoint (input/log/reward).
    #[arg(long, default_value = "http://localhost:50050")]
    thalamus: String,

    /// Address to host the RustTask control service on (delegate connects here).
    #[arg(long, default_value = "127.0.0.1:50060")]
    listen: String,

    /// Monitor index of the subject display (this rig: 1 = HDMI-0 @ 240 Hz;
    /// run `spike --list-monitors` to enumerate).
    #[arg(long, default_value_t = 1)]
    monitor: usize,

    /// Run in a normal window instead of override-redirect fullscreen
    /// (debugging only — fullscreen unredirect is the latency path).
    #[arg(long)]
    windowed: bool,

    /// Frame pacing margin in ms (see render/mod.rs). 0 disables pacing.
    #[arg(long, default_value_t = 1.5)]
    pace_margin_ms: f64,

    #[command(subcommand)]
    command: Option<Command>,
}

#[derive(clap::Subcommand, Debug)]
enum Command {
    /// M1 latency-floor spike: fullscreen photodiode square, toggle timestamps
    /// to CSV, live command-to-photon when the core is running.
    Spike(render::spike::SpikeArgs),
}

fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                // wgpu emits per-frame INFO chatter; keep it to warnings.
                .unwrap_or_else(|_| "info,wgpu_core=warn,wgpu_hal=warn".into()),
        )
        .init();

    let args = Args::parse();

    if let Some(Command::Spike(spike_args)) = args.command {
        // The winit event loop owns the main thread; no server needed.
        return render::spike::run(spike_args);
    }

    let addr: std::net::SocketAddr = args
        .listen
        .parse()
        .map_err(|e| anyhow::anyhow!("invalid --listen address {:?}: {e}", args.listen))?;

    let event_loop = EventLoop::<Wake>::with_user_event().build()?;
    event_loop.set_control_flow(ControlFlow::Poll);
    let proxy = event_loop.create_proxy();
    let (job_tx, job_rx) = std::sync::mpsc::channel();
    // Render thread publishes each frame's shape list; frames() streams it as
    // JPEG to the operator (mirror.rs).
    let (mirror_tx, mirror_rx) = tokio::sync::watch::channel(mirror::MirrorScene::default());

    let endpoint = args.thalamus.clone();
    tracing::info!(thalamus = %endpoint, listen = %args.listen, "starting RustTask executor");
    std::thread::Builder::new()
        .name("grpc-server".into())
        .spawn(move || {
            let rt = tokio::runtime::Builder::new_multi_thread()
                .enable_all()
                .build()
                .expect("tokio runtime");
            let service = RustTaskService {
                thalamus_endpoint: endpoint,
                job_tx: std::sync::Mutex::new(job_tx),
                wake: std::sync::Mutex::new(proxy),
                mirror_rx,
            };
            let result = rt.block_on(
                Server::builder()
                    .add_service(RustTaskServer::new(service))
                    .serve(addr),
            );
            if let Err(e) = result {
                tracing::error!("gRPC server exited: {e:#}");
                std::process::exit(1);
            }
        })?;

    let mut app = Executor::new(
        ExecutorArgs {
            monitor: args.monitor,
            windowed: args.windowed,
            pace_margin_ms: args.pace_margin_ms,
        },
        job_rx,
        mirror_tx,
    );
    event_loop.run_app(&mut app)?;
    Ok(())
}
