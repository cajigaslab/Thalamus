use std::cell::RefCell;
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
struct ThalamusAPI {
    state_is_dict: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    state_is_list: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    state_is_string: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    state_is_int: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    state_is_float: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    state_is_null: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    state_is_bool: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    state_get_string: unsafe extern "C" fn(arg1: *mut ThalamusState) -> *const ::std::os::raw::c_char,
    state_get_int: unsafe extern "C" fn(arg1: *mut ThalamusState) -> i64,
    state_get_float: unsafe extern "C" fn(arg1: *mut ThalamusState) -> f64,
    state_get_bool: unsafe extern "C" fn(arg1: *mut ThalamusState) -> ::std::os::raw::c_char,
    state_get_at_name: unsafe extern "C" fn(
        arg1: *mut ThalamusState,
        arg2: *const ::std::os::raw::c_char,
    ) -> *mut ThalamusState,
    state_get_at_index: unsafe extern "C" fn(arg1: *mut ThalamusState, arg2: usize) -> *mut ThalamusState,
    state_dec_ref: unsafe extern "C" fn(arg1: *mut ThalamusState),
    state_inc_ref: unsafe extern "C" fn(arg1: *mut ThalamusState),
    state_recursive_change_connect: unsafe extern "C" fn(
        state: *mut ThalamusState,
        callback: ThalamusStateRecursiveCallback,
        data: *mut ::std::os::raw::c_void,
    ) -> *mut ThalamusStateConnection,
    state_recursive_change_disconnect: unsafe extern "C" fn(state: *mut ThalamusStateConnection),
    timer_create: unsafe extern "C" fn() -> *mut ThalamusTimer,
    timer_destroy: unsafe extern "C" fn(arg1: *mut ThalamusTimer),
    timer_expire_after_ns: unsafe extern "C" fn(arg1: *mut ThalamusTimer, arg2: usize),
    timer_async_wait:  unsafe extern "C" fn(
        arg1: *mut ThalamusTimer,
        arg2: ThalamusTimerCallback,
        arg3: *mut ::std::os::raw::c_void,
    ),
    error_code_value: unsafe extern "C" fn(arg1: *mut ThalamusErrorCode) -> ::std::os::raw::c_int,
    node_ready: unsafe extern "C" fn(arg1: *const ThalamusNode),
    time_ns: unsafe extern "C" fn() -> u64,
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

mod thalamus {
  use std::os::raw::c_void;
  use std::ptr;
  use std::time::{Duration,Instant};
  use std::ffi::CStr;
  use crate::ThalamusStateConnection;
  use crate::ThalamusAPI;
  use crate::ThalamusState;
  use crate::ThalamusTimer;
  use crate::ThalamusErrorCode;
  use crate::ThalamusNode;
  use std::rc::Rc;
  use std::cell::RefCell;

  pub fn time(api: &ThalamusAPI) -> Duration {
    unsafe {
      return Duration::from_nanos((api.time_ns)());
    }
  }
 
  //type OnStateChangedCallback = Fn(source: &State, action: i32, key: &State, value: &State);

  pub struct State {
    pub state: *mut ThalamusState,
    pub api: &'static ThalamusAPI
  }

  unsafe extern "C" fn state_on_change(source_raw: *mut ThalamusState, action: i32, key_raw: *mut ThalamusState, value_raw: *mut ThalamusState, data: *mut ::std::os::raw::c_void) {
    let args = &mut *(data as *mut StateConnectionCallbackArgs);

    let source = State {state: source_raw, api: args.api};
    let key = State {state: key_raw, api: args.api};
    let value = State {state: value_raw, api: args.api};

    let callback = &mut*args.callback;
    callback.on_change(&source, action, &key, &value);
  }

  unsafe extern "C" fn timer_on_timer(error: *mut ThalamusErrorCode, data: *mut ::std::os::raw::c_void) {
    let args = &mut *(data as *mut TimerCallbackArgs);

    let error_code = (args.api.error_code_value)(error);

    let callback = &mut*args.callback;
    callback.on_timer(ErrorCode {value: error_code});
  }

  struct StateConnectionCallbackArgs {
    pub callback: *mut dyn StateListener,
    pub api: &'static ThalamusAPI
  }

  struct TimerCallbackArgs {
    pub callback: *mut dyn TimerListener,
    pub api: &'static ThalamusAPI
  }

  pub struct ErrorCode {
    pub value: i32
  }

  pub trait StateListener {
      fn on_change(&mut self, source: &State, action: i32, key: &State, value: &State);
  }
  pub trait TimerListener {
      fn on_timer(&mut self, error: ErrorCode);
  }

  impl State {
    pub fn get_string(&self) -> &str {
      unsafe {
        let ptr = (self.api.state_get_string)(self.state);
        CStr::from_ptr(ptr).to_str().unwrap()
      }
    }
    pub fn get_float(&self) -> f64 {
      unsafe {
        (self.api.state_get_float)(self.state)
      }
    }
    pub fn get_bool(&self) -> bool {
      unsafe {
        (self.api.state_get_bool)(self.state) != 0
      }
    }
    pub fn get_int(&self) -> i64 {
      unsafe {
        (self.api.state_get_int)(self.state)
      }
    }

    pub fn is_string(&self) -> bool {
      unsafe {
        (self.api.state_is_string)(self.state) != 0
      }
    }
    pub fn is_float(&self) -> bool {
      unsafe {
        (self.api.state_is_float)(self.state) != 0
      }
    }
    pub fn is_bool(&self) -> bool {
      unsafe {
        (self.api.state_is_bool)(self.state) != 0
      }
    }
    pub fn is_int(&self) -> bool {
      unsafe {
        (self.api.state_is_int)(self.state) != 0
      }
    }

    pub fn connect<T>(&self, callback_raw: &T) -> StateConnection
    where T: StateListener + 'static
    {
      unsafe {
        let q = callback_raw as *const dyn StateListener;
        let callback_ptr = q as *mut dyn StateListener;
        //let callback_ptr = callback_raw.as_mut() as *mut dyn StateListener;
        let callback_args = Box::new(StateConnectionCallbackArgs {
          callback: callback_ptr,
          api: self.api
        });
        let raw = Box::into_raw(callback_args);
        let void_ptr = raw as *mut c_void;
        let connection = (self.api.state_recursive_change_connect)(self.state, Some(state_on_change), void_ptr);
        StateConnection { api: self.api, connection, callback: raw }
      }
    }
  }

  pub struct StateConnection {
    pub api: &'static ThalamusAPI,
    connection: *mut ThalamusStateConnection,
    callback: *mut StateConnectionCallbackArgs
  }

  impl Drop for StateConnection {
    fn drop(&mut self) {
      unsafe {
        (self.api.state_recursive_change_disconnect)(self.connection);
        drop(Box::from_raw(self.callback));
      }
      println!("StateConnection::drop");
    }
  }

  pub struct Timer {
    pub timer: *mut ThalamusTimer,
    pub api: &'static ThalamusAPI
  }

  impl Timer {
    pub fn new(api: &'static ThalamusAPI) -> Timer {
      unsafe {
        let timer = (api.timer_create)();
        Timer {
          api, timer
        }
      }
    }

    pub fn expires_after(&self, duration: Duration) {
      unsafe {
        (self.api.timer_expire_after_ns)(self.timer, duration.as_nanos() as usize);
      }
    }
    pub fn async_wait<T>(&self, listener: &T)
    where T: TimerListener + 'static 
    {
      let const_ptr = listener as *const dyn TimerListener;
      let mut_ptr = const_ptr as *mut dyn TimerListener;

      let args = Box::new(TimerCallbackArgs {
          callback: mut_ptr,
          api: self.api
        });
      let raw = Box::into_raw(args);
      let void_ptr = raw as *mut c_void;
      unsafe {
        (self.api.timer_async_wait)(self.timer, Some(timer_on_timer), void_ptr);
      }
    }
  }

  impl Drop for Timer {
    fn drop(&mut self) {
      unsafe {
        (self.api.timer_destroy)(self.timer);
      }
    }
  }

  pub trait Node {
    fn api(&self) -> &ThalamusAPI;
    fn base(&self) -> &ThalamusNode;
    fn ready(&self) {
      unsafe {
        (self.api().node_ready)(self.base());
      }
    }
  }
}

#[repr(C)]
struct DemoNode {
  base: ThalamusNode,
  state: thalamus::State,
  state_connection: Option<thalamus::StateConnection>,
  timer: thalamus::Timer,
  running: bool,
  start_time: Duration,
  last_time: Duration,
  frequency: f64,
  amplitude: f64,
  samples: Vec<f64>,
  api: &'static ThalamusAPI
}

const THALAMUS_OPERATION_ABORTED: i32 = 995;

use crate::thalamus::Node;

impl thalamus::Node for DemoNode {
  fn api(&self) -> &ThalamusAPI {
    return self.api
  }
  fn base(&self) -> &ThalamusNode {
    return &self.base
  }
}

impl thalamus::TimerListener for DemoNode {
  fn on_timer(&mut self, error: thalamus::ErrorCode) {
    if error.value == THALAMUS_OPERATION_ABORTED {
      return;
    }
    
    let now = thalamus::time(self.api);
    self.samples.clear();
    while self.last_time < now {
      let elapsed = self.last_time - self.start_time;
      let elapsed_s = elapsed.as_secs_f64();
      self.samples.push(self.amplitude*f64::sin(2.0*3.14*self.frequency*elapsed_s));
      self.last_time += Duration::from_millis(1);
    }
  
    self.ready();
  
    //println!("Tick");
    if self.running {
        self.timer.expires_after(Duration::from_millis(16));
        self.timer.async_wait(self);
    } 
  }
}

impl thalamus::StateListener for DemoNode {
  fn on_change(&mut self, _source: &thalamus::State, _action: i32, key: &thalamus::State, value: &thalamus::State) {
    let key_str = key.get_string();
    println!("DemoNode::on_change {}", key_str);

    match key_str {
      "Running" => 
      {
        let val = value.get_bool();
        self.running = val;
        if self.running {
          self.start_time = thalamus::time(self.api);
          self.last_time = self.start_time;
          self.timer.expires_after(Duration::from_millis(16));
          self.timer.async_wait(self);
        }
      },
      "Amplitude" => {
        let val = value.get_float();
        self.amplitude = val;
      },
      "Frequency" => {
        let val = value.get_float();
        self.frequency = val;
      },
      _ => {}
    }
  }
}

impl DemoNode {

  fn new(api: &'static ThalamusAPI, state: thalamus::State) -> Box<DemoNode> {
    let base = ThalamusNode {
      impl_: ptr::null_mut() as *mut ::std::os::raw::c_void,
      time_ns: None,
      analog: Box::into_raw(Box::new(ThalamusAnalogNode::new())) as *mut ThalamusAnalogNode,
      mocap: ptr::null_mut() as *mut ThalamusMocapNode,
      image: ptr::null_mut() as *mut ThalamusImageNode,
      text: ptr::null_mut() as *mut ThalamusTextNode,
    };
    let mut result = Box::new(DemoNode {
      base: base,
      state,
      state_connection: None,
      timer: thalamus::Timer::new(api),
      running: false,
      start_time: Duration::from_millis(0),
      last_time: Duration::from_millis(0),
      frequency: 0.0,
      amplitude: 0.0,
      samples: Vec::<f64>::new(),
      api
    });

    let connection = Some(result.state.connect(result.as_ref()));
    result.state_connection = connection;

    result
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
  let mut result = DemoNode::new(api, thalamus::State{ state, api });
  
  result.base.time_ns = Some(demo_node_time_ns);
  (*result.base.analog).data = Some(demo_node_data);
  (*result.base.analog).short_data = None;
  (*result.base.analog).int_data = None;
  (*result.base.analog).ulong_data = None;
  (*result.base.analog).num_channels = Some(demo_node_num_channels);
  (*result.base.analog).sample_interval_ns = Some(demo_node_sample_interval_ns);
  (*result.base.analog).name = Some(demo_node_name);
  (*result.base.analog).has_analog_data = Some(demo_node_has_analog_data);
  (*result.base.analog).is_short_data = Some(demo_node_is_short_data);
  (*result.base.analog).is_int_data = Some(demo_node_is_short_data);
  (*result.base.analog).is_ulong_data = Some(demo_node_is_short_data);
  (*result.base.analog).is_transformed = Some(demo_node_is_short_data);
  (*result.base.analog).scale = Some(demo_node_scale);
  (*result.base.analog).offset = Some(demo_node_scale);

  let raw = Box::into_raw(result);
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
