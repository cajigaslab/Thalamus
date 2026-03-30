use std::{ffi::CString};
use std::ptr;
use crate::api::{ThalamusAPI};

pub struct ThalamusNode {
    pub c_impl: *mut ::std::os::raw::c_void,
    pub time_ns: ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusNode) -> u64>,
    pub analog: *mut ThalamusAnalogNode,
    //pub mocap: *mut ThalamusMocapNode,
    //pub image: *mut ThalamusImageNode,
    //pub text: *mut ThalamusTextNode,
    pub rust_impl: *mut ::std::os::raw::c_void,
}

pub struct ThalamusAnalogNode {
    pub data: ::std::option::Option<
        unsafe extern "C" fn(
            *mut ThalamusDoubleSpan,
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ),
    >,
    pub short_data: ::std::option::Option<
        unsafe extern "C" fn(
            *mut ThalamusShortSpan,
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ),
    >,
    pub int_data: ::std::option::Option<
        unsafe extern "C" fn(
            *mut ThalamusIntSpan,
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ),
    >,
    pub ulong_data: ::std::option::Option<
        unsafe extern "C" fn(
            *mut ThalamusULongSpan,
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ),
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
    pub name_span: ::std::option::Option<
        unsafe extern "C" fn(
            *mut ThalamusByteSpan,
            node: *mut ThalamusNode,
            channel: ::std::os::raw::c_int,
        ),
    >,
}

impl ThalamusAnalogNode {
  pub fn new() -> ThalamusAnalogNode {
    ThalamusAnalogNode {
      data: None,
      short_data: None,
      int_data: None,
      ulong_data: None,
      num_channels: None,
      sample_interval_ns: None,
      name: None,
      has_analog_data: None,
      is_short_data: None,
      is_int_data: None,
      is_ulong_data: None,
      is_transformed: None,
      scale: None,
      offset: None,
      name_span: None
    }
  }
}

pub const THALAMUS_STATE_ACTION_SET: ThalamusStateAction = 0;
pub const THALAMUS_STATE_ACTION_DELETE: ThalamusStateAction = 1;
pub type ThalamusStateAction = ::std::os::raw::c_int;

pub type ThalamusStateRecursiveCallback = ::std::option::Option<
    unsafe extern "C" fn(
        source: *mut ThalamusState,
        action: ThalamusStateAction,
        key: *mut ThalamusState,
        value: *mut ThalamusState,
        data: *mut ::std::os::raw::c_void,
    ),
>;

pub type IoContextPostCallback = ::std::option::Option<
    unsafe extern "C" fn(data: *mut ::std::os::raw::c_void),
>;

pub type ThalamusIOCallback = ::std::option::Option<
    unsafe extern "C" fn(err: *mut ThalamusErrorCode, count: usize, data: *mut ::std::os::raw::c_void),
>;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusAPIRaw {
    pub state_is_dict: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    pub state_is_list: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    pub state_is_string: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    pub state_is_int: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    pub state_is_float: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    pub state_is_null: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    pub state_is_bool: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    pub state_get_string: unsafe extern "C" fn(arg1: *mut ThalamusState) -> *const ::std::os::raw::c_char,
    pub state_get_int: unsafe extern "C" fn(arg1: *mut ThalamusState) -> i64,
    pub state_get_float: unsafe extern "C" fn(arg1: *mut ThalamusState) -> f64,
    pub state_get_bool: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    pub state_get_at_name: unsafe extern "C" fn(
        arg1: *mut ThalamusState,
        arg2: *const ::std::os::raw::c_char,
    ) -> *mut ThalamusState,
    pub state_get_at_index: unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: usize) -> *mut ThalamusState,
    pub state_dec_ref: unsafe extern "C" fn(arg1: *mut ThalamusState),
    pub state_inc_ref: unsafe extern "C" fn(arg1: *mut ThalamusState),
    pub state_recursive_change_connect: unsafe extern "C" fn(
        state: *mut ThalamusState,
        callback: ThalamusStateRecursiveCallback,
        data: *mut ::std::os::raw::c_void,
    ) -> *mut ThalamusStateConnection,
    pub state_recursive_change_disconnect: unsafe extern "C" fn(state: *mut ThalamusStateConnection),
    pub timer_create: unsafe extern "C" fn() -> *mut ThalamusTimer,
    pub timer_destroy: unsafe extern "C" fn(arg1: *mut ThalamusTimer),
    pub timer_expire_after_ns: unsafe extern "C" fn(arg1: *mut ThalamusTimer, arg2: usize),
    pub timer_async_wait:  unsafe extern "C" fn(
        arg1: *mut ThalamusTimer,
        arg2: ThalamusTimerCallback,
        arg3: *mut ::std::os::raw::c_void,
    ),
    pub error_code_value: unsafe extern "C" fn(arg1: *mut ThalamusErrorCode) -> ::std::os::raw::c_int,
    pub node_ready: unsafe extern "C" fn(arg1: *const ThalamusNode),
    pub time_ns: unsafe extern "C" fn() -> u64,
    pub error_code_operation_aborted: unsafe extern "C" fn() -> i32,
    pub state_recap: unsafe extern "C" fn(arg1: *mut ThalamusState),

    pub state_set_at_name_state: unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: *const ::std::os::raw::c_char, arg3: *mut ThalamusState),
    pub state_set_at_name_string: unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: *const ::std::os::raw::c_char, arg3: *const ::std::os::raw::c_char),
    pub state_set_at_name_int: unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: *const ::std::os::raw::c_char, arg3: i64),
    pub state_set_at_name_float: unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: *const ::std::os::raw::c_char, arg3: f64),
    pub state_set_at_name_null: unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: *const ::std::os::raw::c_char),
    pub state_set_at_name_bool: unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: *const ::std::os::raw::c_char, i8),
//
    pub state_set_at_index_state: unsafe extern "C" fn(arg1: *mut ThalamusState, i64, arg3: *mut ThalamusState),
    pub state_set_at_index_string: unsafe extern "C" fn(arg1: *mut ThalamusState, i64, arg3: *const ::std::os::raw::c_char),
    pub state_set_at_index_int: unsafe extern "C" fn(arg1: *mut ThalamusState, i64, arg3: i64),
    pub state_set_at_index_float: unsafe extern "C" fn(arg1: *mut ThalamusState, i64, arg3: f64),
    pub state_set_at_index_null: unsafe extern "C" fn(arg1: *mut ThalamusState, i64),
    pub state_set_at_index_bool: unsafe extern "C" fn(arg1: *mut ThalamusState, i64, i8),

    pub io_context_post: unsafe extern "C" fn(arg1: IoContextPostCallback, data: *mut ::std::os::raw::c_void),

    pub trace_event_begin: unsafe extern "C" fn(arg1: *const ::std::os::raw::c_char),

    pub trace_event_begin_span: unsafe extern "C" fn(arg1: *mut ::std::os::raw::c_void, arg2: usize),

    pub trace_event_end: unsafe extern "C" fn(),

    pub serial_port_create: unsafe extern "C" fn() -> *mut ThalamusSerialPort,

    pub serial_port_destroy: unsafe extern "C" fn(*mut ThalamusSerialPort),

    pub serial_set_baud_rate: unsafe extern "C" fn(*mut ThalamusSerialPort, u32),

    pub serial_port_open: unsafe extern "C" fn(*mut ThalamusSerialPort, *const ::std::os::raw::c_char),

    pub serial_port_error: unsafe extern "C" fn(*mut ThalamusSerialPort) -> *mut ThalamusErrorCode,
    
    pub serial_port_read_until: unsafe extern "C" fn(*mut ThalamusSerialPort, *mut ThalamusStreamBuf, *const ::std::os::raw::c_char, usize, ThalamusIOCallback, *mut ::std::os::raw::c_void),

    pub serial_port_read_some: unsafe extern "C" fn(*mut ThalamusSerialPort, *mut ThalamusByteSpan, ThalamusIOCallback, *mut ::std::os::raw::c_void),

    pub serial_port_read: unsafe extern "C" fn(*mut ThalamusSerialPort, *mut ThalamusByteSpan, ThalamusIOCallback, *mut ::std::os::raw::c_void),

    pub serial_port_write: unsafe extern "C" fn(*mut ThalamusSerialPort, *mut ThalamusByteSpan, ThalamusIOCallback, *mut ::std::os::raw::c_void),

    pub streambuf_create: unsafe extern "C" fn() -> *mut ThalamusStreamBuf,
    pub streambuf_destroy: unsafe extern "C" fn(*mut ThalamusStreamBuf),
    pub streambuf_to_span: unsafe extern "C" fn(*mut ThalamusCharSpan, *mut ThalamusStreamBuf),
    pub streambuf_consume: unsafe extern "C" fn(*mut ThalamusStreamBuf, usize),
    pub streambuf_size: unsafe extern "C" fn(*mut ThalamusStreamBuf) -> usize,
    pub charspan_destroy: unsafe extern "C" fn(*mut ThalamusCharSpan),
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusSerialPort {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusStreamBuf {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusErrorCode {
    _unused: [u8; 0],
}

pub type ThalamusTimerCallback = ::std::option::Option<
    unsafe extern "C" fn(arg1: *mut ThalamusErrorCode, data: *mut ::std::os::raw::c_void),
>;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusStateConnection {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusTimer {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusState {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusIoContext {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusNodeGraph {
    _unused: [u8; 0],
}
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusDoubleSpan {
    pub data: *const f64,
    pub size: usize,
}
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusShortSpan {
    pub data: *const ::std::os::raw::c_short,
    pub size: usize,
}
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusIntSpan {
    pub data: *const ::std::os::raw::c_int,
    pub size: usize,
}
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusULongSpan {
    pub data: *const u64,
    pub size: usize,
}
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusCharSpan {
    pub data: *const i8,
    pub size: usize,
    pub owns_data: i8,
}
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusByteSpan {
    pub data: *const u8,
    pub size: usize,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusMocapNode {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusImageNode {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusTextNode {
    _unused: [u8; 0],
}

#[repr(C)]
pub struct ThalamusNodeFactory {
    type_: *const ::std::os::raw::c_char,
    create: ::std::option::Option<
        unsafe extern "C" fn(
            factory: *mut ThalamusNodeFactory,
            arg1: *mut ThalamusState,
            arg2: *mut ThalamusIoContext,
            arg3: *mut ThalamusNodeGraph,
        ) -> *mut ThalamusNode,
    >,
    destroy: ::std::option::Option<unsafe extern "C" fn(factory: *mut ThalamusNodeFactory, arg1: *mut ThalamusNode)>,
    prepare: ::std::option::Option<unsafe extern "C" fn(factory: *mut ThalamusNodeFactory) -> ::std::os::raw::c_char>,
    cleanup: ::std::option::Option<unsafe extern "C" fn(factory: *mut ThalamusNodeFactory)>,
    api: *mut ThalamusAPIRaw,
    c_str: CString
}

pub extern "C" fn c_node_data<T: crate::api::AnalogNode>(output: *mut ThalamusDoubleSpan, raw_node: *mut ThalamusNode, channel: ::std::os::raw::c_int) {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };

  let result = node.data(channel);
  unsafe {
    (&mut *output).data = result.as_ptr();
    (&mut *output).size = result.len();
  }
}

pub extern "C" fn c_node_num_channels<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode) -> i32 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  node.num_channels()
}

pub extern "C" fn c_node_sample_interval_ns<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode, channel: ::std::os::raw::c_int) -> u64 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  node.sample_interval(channel).as_nanos() as u64
}

pub extern "C" fn c_node_name<T: crate::api::AnalogNode>(_raw_node: *mut ThalamusNode, _channel: ::std::os::raw::c_int) -> *const i8 {
  ptr::null()
}

pub extern "C" fn c_node_name_span<T: crate::api::AnalogNode>(output: *mut ThalamusByteSpan, raw_node: *mut ThalamusNode, channel: ::std::os::raw::c_int) {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  let result = node.name(channel);
  unsafe {
    (&mut *output).data = result.as_ptr();
    (&mut *output).size = result.len();
  }
}
#[allow(non_snake_case)]
pub extern "C" fn c_node_has_analog_data<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode) -> i8 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  if node.has_analog_data() {1}else{0}
}
#[allow(non_snake_case)]
pub extern "C" fn c_node_is_short_data<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode) -> i8 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  if node.is_short_data() {1}else{0}
}
#[allow(non_snake_case)]
pub extern "C" fn c_node_is_int_data<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode) -> i8 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  if node.is_int_data() {1}else{0}
}
#[allow(non_snake_case)]
pub extern "C" fn c_node_is_ulong_data<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode) -> i8 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  if node.is_ulong_data() {1}else{0}
}
#[allow(non_snake_case)]
pub extern "C" fn c_node_is_transformed<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode) -> i8 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  if node.is_transformed() {1}else{0}
}
#[allow(non_snake_case)]
pub extern "C" fn c_node_scale<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode, channel: i32) -> f64 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  node.scale(channel)
}
#[allow(non_snake_case)]
pub extern "C" fn c_node_offset<T: crate::api::AnalogNode>(raw_node: *mut ThalamusNode, channel: i32) -> f64 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  node.offset(channel)
}
#[allow(non_snake_case)]
pub extern "C" fn c_node_time_ns<T: crate::api::Node>(raw_node: *mut ThalamusNode) -> u64 {
  let c_node = unsafe { &*(raw_node as *const ThalamusNode) };
  let node = unsafe { &*(c_node.rust_impl as *const T) };
  node.time().as_nanos() as u64
}

pub trait WrappableNode {
  fn wrap(&self, c_node: &mut ThalamusNode);
}

extern "C" fn create_node_template<T: crate::api::Node + WrappableNode>(factory: *mut ThalamusNodeFactory, state: *mut ThalamusState, _io_context: *mut ThalamusIoContext, _graph: *mut ThalamusNodeGraph) -> *mut ThalamusNode {
  println!("create_node_template");
  let api_raw = unsafe { (*factory).api };
  let c_node = Box::into_raw(Box::new(ThalamusNode {
    c_impl: ptr::null_mut() as *mut ::std::os::raw::c_void,
    time_ns: None,
    analog: ptr::null_mut() as *mut ThalamusAnalogNode,
    //mocap: ptr::null_mut() as *mut ThalamusMocapNode,
    //image: ptr::null_mut() as *mut ThalamusImageNode,
    //text: ptr::null_mut() as *mut ThalamusTextNode,
    rust_impl: ptr::null_mut() as *mut ::std::os::raw::c_void
  }));
  let c_node_ref = unsafe {&mut*c_node};
  let api = ThalamusAPI{raw: api_raw, node: c_node};
  
  let result = Box::new(T::new(api, crate::api::State::new(api, state)));

  c_node_ref.time_ns = Some(c_node_time_ns::<T>);

  result.wrap(c_node_ref);
  c_node_ref.rust_impl = Box::into_raw(result) as *mut ::std::os::raw::c_void;
  c_node
}

unsafe extern "C" fn destroy_node_template<T>(_factory: *mut ThalamusNodeFactory, node_raw: *mut ThalamusNode) {
  println!("destroy_node_template");
  unsafe {
    let node = &*node_raw;
    //let rust_ptr = node.rust_impl as *mut T;
    drop(Box::from_raw(node.rust_impl as *mut T));
    drop(Box::from_raw(node_raw));
  }
}

impl ThalamusNodeFactory {
  pub fn new<T: crate::api::Node+WrappableNode>(name: &str, api: *mut ThalamusAPIRaw) -> *mut ThalamusNodeFactory {
    println!("ThalamusNodeFactory::new {}", name);
    let c_name = CString::new(name).unwrap();
    let result = Box::into_raw(Box::new(ThalamusNodeFactory {
      type_: c_name.as_ptr(),
      create: Some(create_node_template::<T>),
      destroy: Some(destroy_node_template::<T>),
      prepare: None,
      cleanup: None, 
      api: api,
      c_str: c_name
    }));
    result as *mut ThalamusNodeFactory
  }
}
