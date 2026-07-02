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
///
/// CRITICAL pattern note: tonic's client-streaming calls (`log`,
/// `inject_analog`) resolve only when the REQUEST STREAM ENDS — awaiting them
/// inline deadlocks a long-lived stream before a single message is sent (this
/// silently ate every BehavState log and reward pulse of the first live
/// session). Python fires them without awaiting (`self.stub.inject_analog(
/// queue)`, task_context.py:331); we do the equivalent by spawning the call
/// onto a background task and feeding it through the channel sender.
pub struct ThalamusConn {
    client: ThalamusClient<Channel>,
    log_tx: Option<mpsc::Sender<Text>>,
    /// (node name, sender) — the stream is node-scoped by its first message.
    inject: Option<(String, mpsc::Sender<InjectAnalogRequest>)>,
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
            inject: None,
        })
    }

    /// A second handle over the same multiplexed HTTP/2 channel (fresh stream
    /// senders). Used to open the analog stream concurrently with log/reward
    /// traffic owned by another task.
    pub fn clone_conn(&self) -> ThalamusConn {
        ThalamusConn {
            client: self.client.clone(),
            log_tx: None,
            inject: None,
        }
    }

    /// Open the analog input stream for a node's X/Y channels.
    /// Returns the server->client response stream; parse with input::parse_xy.
    pub async fn analog_xy(
        &mut self,
        node_name: &str,
    ) -> anyhow::Result<tonic::Streaming<AnalogResponse>> {
        self.analog_channels(node_name, &["X", "Y"]).await
    }

    /// Open an analog stream for arbitrary named channels of a node.
    pub async fn analog_channels(
        &mut self,
        node_name: &str,
        channel_names: &[&str],
    ) -> anyhow::Result<tonic::Streaming<AnalogResponse>> {
        let req = AnalogRequest {
            node: Some(NodeSelector {
                name: node_name.to_string(),
                r#type: String::new(),
            }),
            channels: vec![],
            channel_names: channel_names.iter().map(|s| s.to_string()).collect(),
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
    ///
    /// The RPC future is spawned, NOT awaited (see the struct docs): messages
    /// flow as they are queued, and the future only resolves when the stream
    /// closes or errors.
    fn log_sender(&mut self) -> mpsc::Sender<Text> {
        if let Some(tx) = &self.log_tx {
            if !tx.is_closed() {
                return tx.clone();
            }
            // Stream died (core restart, transport error): reopen.
            self.log_tx = None;
        }
        let (tx, rx) = mpsc::channel::<Text>(256);
        let outbound = ReceiverStream::new(rx);
        let mut client = self.client.clone();
        tokio::spawn(async move {
            if let Err(status) = client.log(outbound).await {
                tracing::warn!(%status, "log stream terminated");
            }
        });
        self.log_tx = Some(tx.clone());
        tx
    }

    /// Send a single behavioral log line already stamped in the Python perf_counter
    /// domain (see clock.rs). `text` is e.g. "BehavState=start_on".
    pub async fn log(&mut self, text: String, time_python_ns: u64) -> anyhow::Result<()> {
        let message = Text {
            text,
            time: time_python_ns,
            remote_time: 0,
            redirect: String::new(),
        };
        let tx = self.log_sender();
        if let Err(e) = tx.send(message).await {
            // The stream broke since we cached it; reopen once and retry.
            self.log_tx = None;
            let tx = self.log_sender();
            tx.send(e.0).await.context("log stream unavailable")?;
        }
        Ok(())
    }

    /// Lazily open the `inject_analog(stream InjectAnalogRequest)` RPC targeting a
    /// node. The FIRST message must carry the node name (task_context.py:331-334,
    /// which keeps one stream per node fed by an un-awaited queue — mirrored here).
    fn inject_sender(&mut self, node_name: &str) -> mpsc::Sender<InjectAnalogRequest> {
        if let Some((cached_node, tx)) = &self.inject {
            if cached_node == node_name && !tx.is_closed() {
                return tx.clone();
            }
            self.inject = None;
        }
        let (tx, rx) = mpsc::channel::<InjectAnalogRequest>(64);
        let outbound = ReceiverStream::new(rx);
        let mut client = self.client.clone();
        tokio::spawn(async move {
            if let Err(status) = client.inject_analog(outbound).await {
                tracing::warn!(%status, "inject_analog stream terminated");
            }
        });
        // First message names the node. try_send cannot fail on a fresh channel.
        let _ = tx.try_send(InjectAnalogRequest {
            body: Some(thalamus::inject_analog_request::Body::Node(
                node_name.to_string(),
            )),
        });
        self.inject = Some((node_name.to_string(), tx.clone()));
        tx
    }

    /// Inject one analog signal payload (e.g. a reward pulse) to a node.
    pub async fn inject_signal(
        &mut self,
        node_name: &str,
        signal: AnalogResponse,
    ) -> anyhow::Result<()> {
        let message = InjectAnalogRequest {
            body: Some(thalamus::inject_analog_request::Body::Signal(signal)),
        };
        let tx = self.inject_sender(node_name);
        if let Err(e) = tx.send(message).await {
            self.inject = None;
            let tx = self.inject_sender(node_name);
            tx.send(e.0).await.context("inject_analog stream unavailable")?;
        }
        Ok(())
    }
}
