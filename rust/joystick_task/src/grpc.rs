//! Thin async gRPC client wrappers around the `Thalamus` service.
//!
//! Mirrors the subset of `TaskContext` methods the Python joystick_intro uses:
//!   - analog stream (input)      -> TaskContext analog (joystick_intro.py:2848-2855)
//!   - log stream (BehavState)    -> TaskContext.log (task_context.py:406)
//!   - inject_analog (reward)     -> TaskContext.inject_analog (task_context.py:337)
//!
//! Connection is an ordinary INSECURE channel to localhost:50050 — identical to
//! the Python controller (main_impl.py:180), the .NET client (dotnet/NodeGraph.cs)
//! and the Java client (java/.../App.java:29). No auth.

use anyhow::Context as _;
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;
use tonic::transport::Channel;

use crate::proto::thalamus::{
    self, thalamus_client::ThalamusClient, AnalogRequest, AnalogResponse, InjectAnalogRequest,
    NodeSelector, Text,
};

/// Connected Thalamus client plus the long-lived sender halves of the streaming
/// RPCs we keep open for the duration of the process.
pub struct ThalamusConn {
    client: ThalamusClient<Channel>,
    log_tx: Option<mpsc::Sender<Text>>,
    inject_tx: Option<mpsc::Sender<InjectAnalogRequest>>,
}

impl ThalamusConn {
    /// Connect to the core. `endpoint` e.g. "http://localhost:50050".
    pub async fn connect(endpoint: String) -> anyhow::Result<Self> {
        let client = ThalamusClient::connect(endpoint.clone())
            .await
            .with_context(|| format!("connecting to Thalamus core at {endpoint}"))?;
        Ok(Self {
            client,
            log_tx: None,
            inject_tx: None,
        })
    }

    /// Open the analog input stream for a node's X/Y channels.
    /// Returns the server->client response stream; parse with input::parse_xy.
    pub async fn analog_xy(
        &mut self,
        node_name: &str,
    ) -> anyhow::Result<tonic::Streaming<AnalogResponse>> {
        let req = AnalogRequest {
            node: Some(NodeSelector {
                name: node_name.to_string(),
                r#type: String::new(),
            }),
            channels: vec![],
            channel_names: vec!["X".to_string(), "Y".to_string()],
        };
        let stream = self
            .client
            .analog(req)
            .await
            .context("opening analog stream")?
            .into_inner();
        Ok(stream)
    }

    /// Lazily open the `log(stream Text)` RPC and return the sender.
    /// All BehavState markers go here; they land in the storage capture file with
    /// an empty `node` field (storage2_node.cpp on_log), byte-identical to Python.
    pub async fn log_sender(&mut self) -> anyhow::Result<mpsc::Sender<Text>> {
        if let Some(tx) = &self.log_tx {
            return Ok(tx.clone());
        }
        let (tx, rx) = mpsc::channel::<Text>(256);
        let outbound = ReceiverStream::new(rx);
        // Fire-and-forget: the server consumes until we drop the sender.
        self.client.log(outbound).await.context("opening log stream")?;
        self.log_tx = Some(tx.clone());
        Ok(tx)
    }

    /// Send a single behavioral log line already stamped in the Python perf_counter
    /// domain (see clock.rs). `text` is e.g. "BehavState=start_on".
    pub async fn log(&mut self, text: String, time_python_ns: u64) -> anyhow::Result<()> {
        let tx = self.log_sender().await?;
        tx.send(Text {
            text,
            time: time_python_ns,
            remote_time: 0,
            redirect: String::new(),
        })
        .await
        .context("queueing log Text")?;
        Ok(())
    }

    /// Lazily open the `inject_analog(stream InjectAnalogRequest)` RPC targeting a
    /// node. The FIRST message must carry the node name (task_context.py:333); we
    /// send it here on open.
    pub async fn inject_sender(
        &mut self,
        node_name: &str,
    ) -> anyhow::Result<mpsc::Sender<InjectAnalogRequest>> {
        if let Some(tx) = &self.inject_tx {
            return Ok(tx.clone());
        }
        let (tx, rx) = mpsc::channel::<InjectAnalogRequest>(64);
        let outbound = ReceiverStream::new(rx);
        self.client
            .inject_analog(outbound)
            .await
            .context("opening inject_analog stream")?;
        // First message names the node.
        tx.send(InjectAnalogRequest {
            body: Some(thalamus::inject_analog_request::Body::Node(
                node_name.to_string(),
            )),
        })
        .await
        .context("sending inject_analog node header")?;
        self.inject_tx = Some(tx.clone());
        Ok(tx)
    }

    /// Inject one analog signal payload (e.g. a reward pulse) to a node.
    pub async fn inject_signal(
        &mut self,
        node_name: &str,
        signal: AnalogResponse,
    ) -> anyhow::Result<()> {
        let tx = self.inject_sender(node_name).await?;
        tx.send(InjectAnalogRequest {
            body: Some(thalamus::inject_analog_request::Body::Signal(signal)),
        })
        .await
        .context("queueing inject_analog signal")?;
        Ok(())
    }
}
