#![allow(dead_code)]
#![allow(non_snake_case)]
#![allow(non_camel_case_types)]
#![allow(non_upper_case_globals)]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

use std::ptr;
use std::time::{Duration,Instant};

type ThalamusNodeCreate = unsafe extern "C" fn(*mut ThalamusState, *mut ThalamusIoContext, *mut ThalamusNodeGraph) -> *mut ThalamusNode;
type ThalamusNodeDestroy = unsafe extern "C" fn(arg1: *mut ThalamusNode);
type ThalamusNodePrepare = unsafe extern "C" fn() -> ::std::os::raw::c_char;
type ThalamusNodeCleanup = unsafe extern "C" fn();

trait Node {
  fn ready(&self);
  fn time(&self, channel: i32) -> Duration;
}

trait AnalogNode {
  fn data(&self, channel: i32) -> &[f64];
  fn short_data(&self, channel: i32) -> &[i16] {
    panic!("Unimplemented");
  }
  fn int_data(&self, channel: i32) -> &[i32] {
    panic!("Unimplemented");
  }
  fn ulong_data(&self, channel: i32) -> &[u64] {
    panic!("Unimplemented");
  }
  fn num_channels(&self) -> i32;
  fn sample_interval(&self, channel: i32) -> Duration;
  fn name(&self, channel: i32) -> &str;
  fn has_analog_data(&self) -> bool;
  fn is_short_data(&self) -> bool;
  fn is_int_data(&self) -> bool;
  fn is_ulong_data(&self) -> bool;
  fn is_transformed(&self) -> bool;
  fn scale(&self, int channel) -> f64;
  fn offset(&self, int channel) -> f64;
}

struct ExtNode {
  Vec<f64> samples;
}

impl AnalogNode for ExtNode {
  fn data(&self, channel:i32) -> &[f64] {
    &self.data[..]
  }

  fn num_channels(&self) {
    1
  }

}

pub struct ThalamusNode {
    pub impl_: *mut ::std::os::raw::c_void,
    pub time_ns: ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusNode) -> u64>,
    pub analog: *mut ThalamusAnalogNode,
    pub mocap: *mut ThalamusMocapNode,
    pub image: *mut ThalamusImageNode,
    pub text: *mut ThalamusTextNode,
}

pub struct ThalamusAnalogNode {
    pub data: ::std::option::Option<
        unsafe extern "C" fn(
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ) -> ThalamusDoubleSpan,
    >,
    pub short_data: ::std::option::Option<
        unsafe extern "C" fn(
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ) -> ThalamusShortSpan,
    >,
    pub int_data: ::std::option::Option<
        unsafe extern "C" fn(
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ) -> ThalamusIntSpan,
    >,
    pub ulong_data: ::std::option::Option<
        unsafe extern "C" fn(
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ) -> ThalamusULongSpan,
    >,
    pub num_channels: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode) -> ::std::os::raw::c_int,
    >,
    pub sample_interval_ns: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode, channel: ::std::os::raw::c_int) -> u64,
    >,
    pub name: ::std::option::Option<
        unsafe extern "C" fn(
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ) -> *const ::std::os::raw::c_char,
    >,
    pub has_analog_data: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode) -> ::std::os::raw::c_char,
    >,
    pub is_short_data: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode) -> ::std::os::raw::c_char,
    >,
    pub is_int_data: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode) -> ::std::os::raw::c_char,
    >,
    pub is_ulong_data: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode) -> ::std::os::raw::c_char,
    >,
    pub is_transformed: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode) -> ::std::os::raw::c_char,
    >,
    pub scale: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode, channel: ::std::os::raw::c_int) -> f64,
    >,
    pub offset: ::std::option::Option<
        unsafe extern "C" fn(node: *mut ThalamusNode, channel: ::std::os::raw::c_int) -> f64,
    >,
}

unsafe extern "C" fn create_node(_state: *mut ThalamusState, _io_context: *mut ThalamusIoContext, _graph: *mut ThalamusNodeGraph) -> *mut ThalamusNode {
  ptr::null_mut() as *mut ThalamusNode
}

const demo_node_factory: ThalamusNodeFactory = ThalamusNodeFactory {
    type_: c"EXT_DEMO".as_ptr(),
    create: Some(create_node),
    destroy: None,
    prepare: None,
    cleanup: None
};

const factories: [*const ThalamusNodeFactory; 2] = [
    &demo_node_factory as *const ThalamusNodeFactory,
    ptr::null_mut() as *const ThalamusNodeFactory
];

#[unsafe(no_mangle)]
pub extern "C" fn get_node_factories(_api: *mut ThalamusAPI) -> *const *const ThalamusNodeFactory {
  factories.as_ptr()
}
