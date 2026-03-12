use std::ptr;
use std::time::{Duration,Instant};
use std::ffi::CStr;

//type ThalamusNodeCreate = unsafe extern "C" fn(*mut ThalamusState, *mut ThalamusIoContext, *mut ThalamusNodeGraph) -> *mut ThalamusNode;
//type ThalamusNodeDestroy = unsafe extern "C" fn(arg1: *mut ThalamusNode);
//type ThalamusNodePrepare = unsafe extern "C" fn() -> ::std::os::raw::c_char;
//type ThalamusNodeCleanup = unsafe extern "C" fn();
//
//trait Node {
//  fn c() -> *mut ThalamusNode;
//  fn api() -> *mut ThalamusAPI;
//  fn ready(&self) {
//    let node = c()
//    self.api().node_ready(c())
//  }
//  fn time(&self, channel: i32) -> Duration;
//}
//
//trait AnalogNode {
//  fn data(&self, channel: i32) -> &[f64];
//  fn short_data(&self, channel: i32) -> &[i16] {
//    panic!("Unimplemented");
//  }
//  fn int_data(&self, channel: i32) -> &[i32] {
//    panic!("Unimplemented");
//  }
//  fn ulong_data(&self, channel: i32) -> &[u64] {
//    panic!("Unimplemented");
//  }
//  fn num_channels(&self) -> i32;
//  fn sample_interval(&self, channel: i32) -> Duration;
//  fn name(&self, channel: i32) -> &str;
//  fn has_analog_data(&self) -> bool;
//  fn is_short_data(&self) -> bool {
//    false
//  }
//  fn is_int_data(&self) -> bool {
//    false
//  }
//  fn is_ulong_data(&self) -> bool {
//    false
//  }
//  fn is_transformed(&self) -> bool {
//    false
//  }
//  fn scale(&self, int channel) -> f64 {
//    1
//  }
//  fn offset(&self, int channel) -> f64 {
//    0
//  }
//}
//
//struct ExtNode {
//  _c: *mut ThalamusNode
//  Duration current_time;
//  Vec<f64> samples;
//}
//
//static api: *mut ThalamusAPI = ptr::null_mut();
//
//impl Node for ExtNode {
//  c(&self) -> *mut ThalamusNode {
//    self._c
//  }
//  api(&self) -> *mut ThalamusAPI {
//
//  }
//  fn time(&self, channel: i32) -> Duration {
//    self.current_time
//  }
//}
//
//impl AnalogNode for ExtNode {
//  fn data(&self, channel:i32) -> &[f64] {
//    &self.samples[..]
//  }
//
//  fn num_channels(&self) {
//    1
//  }
//
//  fn sample_interval(&self, channel: i32) -> Duration {
//    return Duration::from_millis(1)
//  }
//
//  fn name(&self, channel: i32) -> &str {
//    "Data"
//  }
//  fn has_analog_data(&self) -> bool {
//    true
//  }
//}



struct ThalamusNode {
    pub impl_: *mut ::std::os::raw::c_void,
    pub time_ns: ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusNode) -> u64>,
    pub analog: *mut ThalamusAnalogNode,
    pub mocap: *mut ThalamusMocapNode,
    pub image: *mut ThalamusImageNode,
    pub text: *mut ThalamusTextNode,
}

impl ThalamusNode {
  fn new() -> ThalamusNode {
    ThalamusNode {
      impl_: ptr::null_mut() as *mut ::std::os::raw::c_void,
      time_ns: None,
      analog: ptr::null_mut() as *mut ThalamusAnalogNode,
      mocap: ptr::null_mut() as *mut ThalamusMocapNode,
      image: ptr::null_mut() as *mut ThalamusImageNode,
      text: ptr::null_mut() as *mut ThalamusTextNode,
    }
  }
}

struct ThalamusAnalogNode {
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

impl ThalamusAnalogNode {
  fn new() -> ThalamusAnalogNode {
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

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct ThalamusAPI {
    pub state_is_dict: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    >,
    pub state_is_list: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    >,
    pub state_is_string: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    >,
    pub state_is_int: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    >,
    pub state_is_float: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    >,
    pub state_is_null: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    >,
    pub state_is_bool: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    >,
    pub state_get_string: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> *const ::std::os::raw::c_char,
    >,
    pub state_get_int: ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusState) -> i64>,
    pub state_get_float:
        ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusState) -> f64>,
    pub state_get_bool: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    >,
    pub state_get_at_name: ::std::option::Option<
        unsafe extern "C" fn(
            arg1: *mut ThalamusState,
            arg2: *const ::std::os::raw::c_char,
        ) -> *mut ThalamusState,
    >,
    pub state_get_at_index: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: usize) -> *mut ThalamusState,
    >,
    pub state_dec_ref: ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusState)>,
    pub state_inc_ref: ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusState)>,
    pub state_recursive_change_connect: ::std::option::Option<
        unsafe extern "C" fn(
            state: *mut ThalamusState,
            callback: ThalamusStateRecursiveCallback,
            data: *mut ::std::os::raw::c_void,
        ) -> *mut ThalamusStateConnection,
    >,
    pub state_recursive_change_disconnect:
        ::std::option::Option<unsafe extern "C" fn(state: *mut ThalamusStateConnection)>,
    pub timer_create: ::std::option::Option<unsafe extern "C" fn() -> *mut ThalamusTimer>,
    pub timer_destroy: ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusTimer)>,
    pub timer_expire_after_ns:
        ::std::option::Option<unsafe extern "C" fn(arg1: *mut ThalamusTimer, arg2: usize)>,
    pub timer_async_wait: ::std::option::Option<
        unsafe extern "C" fn(
            arg1: *mut ThalamusTimer,
            arg2: ThalamusTimerCallback,
            arg3: *mut ::std::os::raw::c_void,
        ),
    >,
    pub error_code_value: ::std::option::Option<
        unsafe extern "C" fn(arg1: *mut ThalamusErrorCode) -> ::std::os::raw::c_int,
    >,
    pub node_ready: ::std::option::Option<unsafe extern "C" fn(arg1: *const ThalamusNode)>,
    pub time_ns: ::std::option::Option<unsafe extern "C" fn() -> u64>,
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
#[derive(Debug, Copy, Clone)]
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
    api: *mut ThalamusAPI
}


#[repr(C)]
struct DemoNode <'a>{
  base: ThalamusNode,
  state: *mut ThalamusState,
  state_connection: *mut ThalamusStateConnection,
  timer: *mut ThalamusTimer,
  running: bool,
  start_time: Duration,
  last_time: Duration,
  frequency: f64,
  amplitude: f64,
  samples: Vec<f64>,
  api: &'a ThalamusAPI
}

const THALAMUS_OPERATION_ABORTED: i32 = 995;

unsafe extern "C" fn demo_node_on_timer(arg1: *mut ThalamusErrorCode, data: *mut ::std::os::raw::c_void) {
  let node = &mut *(data as *mut DemoNode);
  if node.api.error_code_value.expect("err")(arg1) == THALAMUS_OPERATION_ABORTED {
    return;
  }
  
  let now = Duration::from_nanos(node.api.time_ns.expect("err")());
  node.samples.clear();
  while node.last_time < now {
    let elapsed = node.last_time - node.start_time;
    let elapsed_s = elapsed.as_secs_f64();
    node.samples.push(node.amplitude*f64::sin(2.0*3.14*node.frequency*elapsed_s));
    node.last_time += Duration::from_millis(1);
  }

  node.api.node_ready.expect("err")(&node.base as *const ThalamusNode);

  //println!("Tick");
  if node.running {
    node.api.timer_expire_after_ns.expect("err")(node.timer, 16000000);
    node.api.timer_async_wait.expect("err")(node.timer, Some(demo_node_on_timer), data);
  }
}

unsafe extern "C" fn demo_node_on_change(source: *mut ThalamusState, action: i32, key: *mut ThalamusState, value: *mut ThalamusState, data: *mut ::std::os::raw::c_void) {
  let node = &mut*(data as *mut DemoNode);
  let raw_str = node.api.state_get_string.expect("err")(key);
  let key_str = CStr::from_ptr(raw_str).to_str().unwrap();

  if(node.api.state_is_bool.expect("err")(value) != 0) {
    let val_bool: bool = node.api.state_get_bool.expect("err")(value) != 0;
    node.running = val_bool;
    println!("demo_node_on_change {} {}", key_str, val_bool);

    if(val_bool) {
      node.start_time = Duration::from_nanos(node.api.time_ns.expect("err")());
      node.last_time = node.start_time;
      node.api.timer_expire_after_ns.expect("err")(node.timer, 16000000);
      node.api.timer_async_wait.expect("err")(node.timer, Some(demo_node_on_timer), data);
    }
  } else if(node.api.state_is_float.expect("err")(value) != 0) {
    let val_float = node.api.state_get_float.expect("err")(value);
    println!("demo_node_on_change {} {}", key_str, val_float);
    if key_str == "Amplitude" {
      node.amplitude = val_float;
    } else {
      node.frequency = val_float
    }
  }
}

impl<'a> DemoNode<'a> {
  fn new(api: &'a ThalamusAPI) -> DemoNode<'a> {
    let base = ThalamusNode {
      impl_: ptr::null_mut() as *mut ::std::os::raw::c_void,
      time_ns: None,
      analog: Box::into_raw(Box::new(ThalamusAnalogNode::new())) as *mut ThalamusAnalogNode,
      mocap: ptr::null_mut() as *mut ThalamusMocapNode,
      image: ptr::null_mut() as *mut ThalamusImageNode,
      text: ptr::null_mut() as *mut ThalamusTextNode,
    };
    DemoNode {
      base: base,
      state: ptr::null_mut() as *mut ThalamusState,
      state_connection: ptr::null_mut() as *mut ThalamusStateConnection,
      timer: ptr::null_mut() as *mut ThalamusTimer,
      running: false,
      start_time: Duration::from_millis(0),
      last_time: Duration::from_millis(0),
      frequency: 0.0,
      amplitude: 0.0,
      samples: Vec::<f64>::new(),
      api
    }
  }
}

impl<'a> Drop for DemoNode<'a> {
  fn drop(&mut self) {
    unsafe {
      self.api.state_recursive_change_disconnect.expect("err")(self.state_connection);
      self.api.timer_destroy.expect("err")(self.timer);
    }
  }
}

unsafe extern "C" fn demo_node_data(raw_node: *mut ThalamusNode, _channel: ::std::os::raw::c_int) -> ThalamusDoubleSpan {
  let node = &*(raw_node as *const DemoNode);
  ThalamusDoubleSpan { data: node.samples.as_ptr(), size: node.samples.len() }
}
unsafe extern "C" fn demo_node_num_channels(_raw_node: *mut ThalamusNode) -> i32 {
  1
}
unsafe extern "C" fn demo_node_sample_interval_ns(_raw_node: *mut ThalamusNode, _channel: ::std::os::raw::c_int) -> u64 {
  1000000
}
unsafe extern "C" fn demo_node_name(_raw_node: *mut ThalamusNode, _channel: ::std::os::raw::c_int) -> *const i8 {
  c"data".as_ptr()
}
unsafe extern "C" fn demo_node_has_analog_data(_raw_node: *mut ThalamusNode) -> i8 {
  1
}
unsafe extern "C" fn demo_node_is_short_data(_raw_node: *mut ThalamusNode) -> i8 {
  0
}
unsafe extern "C" fn demo_node_scale(_raw_node: *mut ThalamusNode, _channel: i32) -> f64 {
  1.0
}
unsafe extern "C" fn demo_node_time_ns(raw_node: *mut ThalamusNode) -> u64 {
  let node = &*(raw_node as *const DemoNode);
  (node.last_time - Duration::from_millis(1)).as_nanos() as u64
}

unsafe extern "C" fn create_node(factory: *mut ThalamusNodeFactory, state: *mut ThalamusState, _io_context: *mut ThalamusIoContext, _graph: *mut ThalamusNodeGraph) -> *mut ThalamusNode {
  let api = &*(*factory).api;
  let result = Box::new(DemoNode::new(api));
  let raw = Box::into_raw(result);
  
  (*raw).base.time_ns = Some(demo_node_time_ns);
  (*(*raw).base.analog).data = Some(demo_node_data);
  (*(*raw).base.analog).short_data = None;
  (*(*raw).base.analog).int_data = None;
  (*(*raw).base.analog).ulong_data = None;
  (*(*raw).base.analog).num_channels = Some(demo_node_num_channels);
  (*(*raw).base.analog).sample_interval_ns = Some(demo_node_sample_interval_ns);
  (*(*raw).base.analog).name = Some(demo_node_name);
  (*(*raw).base.analog).has_analog_data = Some(demo_node_has_analog_data);
  (*(*raw).base.analog).is_short_data = Some(demo_node_is_short_data);
  (*(*raw).base.analog).is_int_data = Some(demo_node_is_short_data);
  (*(*raw).base.analog).is_ulong_data = Some(demo_node_is_short_data);
  (*(*raw).base.analog).is_transformed = Some(demo_node_is_short_data);
  (*(*raw).base.analog).scale = Some(demo_node_scale);
  (*(*raw).base.analog).offset = Some(demo_node_scale);
  
  (*raw).state = state;
  (*raw).state_connection = (*api).state_recursive_change_connect.expect("err")(state, Some(demo_node_on_change), raw as *mut ::std::os::raw::c_void);
  (*raw).timer = (*api).timer_create.expect("err")();

  raw as *mut ThalamusNode
}

unsafe extern "C" fn destroy_node(factory: *mut ThalamusNodeFactory, node: *mut ThalamusNode) {
  drop(Box::from_raw(node as *mut DemoNode))
}

#[unsafe(no_mangle)]
pub extern "C" fn get_node_factories(api: *mut ThalamusAPI) -> *const *const ThalamusNodeFactory {
  let mut vec = Vec::<*const ThalamusNodeFactory>::new();

  vec.push(Box::into_raw(Box::new(ThalamusNodeFactory {
    type_: c"EXT_DEMO".as_ptr(),
    create: Some(create_node),
    destroy: Some(destroy_node),
    prepare: None,
    cleanup: None,
    api
  })));

  vec.push(ptr::null_mut());
  let ptr = vec.as_ptr();
  std::mem::forget(vec);
  ptr
}
