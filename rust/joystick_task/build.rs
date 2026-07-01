//! Compile-time protobuf code generation.
//!
//! We compile the *existing, unmodified* Thalamus proto contracts plus our own
//! small control-service proto. All protos live in the repo's canonical proto/
//! directory (the same source thalamus/build.py reads); we do not fork copies.
//! Requires `protoc` on PATH (macOS: `brew install protobuf`; Debian/Ubuntu rig:
//! `apt-get install protobuf-compiler`).
//!
//! Generated Rust modules (included in src/proto.rs):
//!   - thalamus_grpc         <- ../../proto/thalamus.proto
//!   - task_controller_grpc  <- ../../proto/task_controller.proto
//!   - util_grpc             <- ../../proto/util.proto  (imported by task_controller)
//!   - rust_task_grpc        <- ../../proto/rust_task.proto  (delegate <-> Rust channel)

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let proto_dir = "../../proto";

    let protos = [
        format!("{proto_dir}/thalamus.proto"),
        format!("{proto_dir}/task_controller.proto"),
        format!("{proto_dir}/util.proto"),
        format!("{proto_dir}/rust_task.proto"),
    ];

    // Rebuild if any proto changes.
    for p in &protos {
        println!("cargo:rerun-if-changed={p}");
    }

    tonic_build::configure()
        .build_server(true) // we host the RustTask service (delegate connects to us)
        .build_client(true) // we are a client of the Thalamus service
        // include dir must contain util.proto so `import "util.proto";` in
        // task_controller.proto resolves.
        .compile_protos(&protos, &[proto_dir])?;

    Ok(())
}
