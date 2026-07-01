//! thalamus-joystick-task: low-latency Rust executor for the joystick_intro
//! behavioral task (real-time BCI patch).
//!
//! Process model: long-lived. Connects to the Thalamus core (localhost:50050) as
//! an ordinary insecure gRPC client for input/log/reward, and HOSTS the RustTask
//! control service (localhost:50060) that the Python delegate calls per trial.
//!
//! See rust/README.md and docs/rust_bci_patch.md for architecture and milestones.

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

use crate::control::RustTaskService;
use crate::proto::rust_task::rust_task_server::RustTaskServer;

#[derive(Parser, Debug)]
#[command(name = "joystick_intro_executor", version, about)]
struct Args {
    /// Thalamus core gRPC endpoint (input/log/reward).
    #[arg(long, default_value = "http://localhost:50050")]
    thalamus: String,

    /// Address to host the RustTask control service on (delegate connects here).
    #[arg(long, default_value = "127.0.0.1:50060")]
    listen: String,
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "info".into()),
        )
        .init();

    let args = Args::parse();
    let addr = args.listen.parse().map_err(|e| {
        anyhow::anyhow!("invalid --listen address {:?}: {e}", args.listen)
    })?;

    tracing::info!(thalamus = %args.thalamus, listen = %args.listen, "starting RustTask executor");

    let service = RustTaskService::new(args.thalamus);

    Server::builder()
        .add_service(RustTaskServer::new(service))
        .serve_with_shutdown(addr, async {
            let _ = tokio::signal::ctrl_c().await;
            tracing::info!("shutdown signal received");
        })
        .await?;

    Ok(())
}
