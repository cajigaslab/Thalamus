//! Generated protobuf/tonic modules.
//!
//! `tonic_build` emits one Rust module per proto `package` into `OUT_DIR`. We
//! re-export them here under friendly names. Field/message names match the
//! `.proto` files exactly (snake_case fields, PascalCase messages).

// Module names must equal the proto package names: the generated
// task_controller_grpc code resolves its util.proto import as
// `super::util_grpc`, so the sibling module has to carry that exact name.

// The protos use snake_case rpc names, so the generated associated types
// (`fooStream`) trip non_camel_case_types — silence style lints on generated
// code only.

pub mod thalamus_grpc {
    #![allow(nonstandard_style, dead_code)]
    // package thalamus_grpc  (thalamus.proto)
    tonic::include_proto!("thalamus_grpc");
}

pub mod task_controller_grpc {
    #![allow(nonstandard_style, dead_code)]
    // package task_controller_grpc  (task_controller.proto)
    tonic::include_proto!("task_controller_grpc");
}

pub mod util_grpc {
    #![allow(nonstandard_style, dead_code)]
    // package util_grpc  (util.proto)
    tonic::include_proto!("util_grpc");
}

pub mod rust_task_grpc {
    #![allow(nonstandard_style, dead_code)]
    // package rust_task_grpc  (proto/rust_task.proto)
    tonic::include_proto!("rust_task_grpc");
}

// Friendly aliases used throughout the crate. Some are not referenced until
// later milestones (M3/M4).
#[allow(unused_imports)]
pub use rust_task_grpc as rust_task;
#[allow(unused_imports)]
pub use task_controller_grpc as task_controller;
#[allow(unused_imports)]
pub use thalamus_grpc as thalamus;
#[allow(unused_imports)]
pub use util_grpc as util;
