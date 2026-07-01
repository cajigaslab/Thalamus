//! Generated protobuf/tonic modules.
//!
//! `tonic_build` emits one Rust module per proto `package` into `OUT_DIR`. We
//! re-export them here under friendly names. Field/message names match the
//! `.proto` files exactly (snake_case fields, PascalCase messages).

pub mod thalamus {
    // package thalamus_grpc  (thalamus.proto)
    tonic::include_proto!("thalamus_grpc");
}

pub mod task_controller {
    // package task_controller_grpc  (task_controller.proto)
    tonic::include_proto!("task_controller_grpc");
}

pub mod util {
    // package util_grpc  (util.proto)
    tonic::include_proto!("util_grpc");
}

pub mod rust_task {
    // package rust_task_grpc  (proto/rust_task.proto)
    tonic::include_proto!("rust_task_grpc");
}
