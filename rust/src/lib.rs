#![allow(dead_code)]
#![allow(non_snake_case)]
#![allow(non_camel_case_types)]
#![allow(non_upper_case_globals)]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

use std::ptr;

type ThalamusNodeCreate = unsafe extern "C" fn(*mut ThalamusState, *mut ThalamusIoContext, *mut ThalamusNodeGraph) -> *mut ThalamusNode;
type ThalamusNodeDestroy = unsafe extern "C" fn(arg1: *mut ThalamusNode);
type ThalamusNodePrepare = unsafe extern "C" fn() -> ::std::os::raw::c_char;
type ThalamusNodeCleanup = unsafe extern "C" fn();

unsafe extern "C" fn create_node(_state: *mut ThalamusState, _io_context: *mut ThalamusIoContext, _graph: *mut ThalamusNodeGraph) -> *mut ThalamusNode {
  ptr::null_mut() as *mut ThalamusNode
}

const EXT_DEMO: &[u8] = b"EXT_DEMO\0";
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
