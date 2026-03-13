
use std::{os::raw::c_void, sync::OnceLock};
use std::time::Duration;
use std::ffi::CStr;

pub use crate::ffi::{
  *
};

pub fn time(api: *const ThalamusAPI) -> Duration {
  unsafe {
    return Duration::from_nanos(((&*api).time_ns)());
  }
}

pub struct State {
  pub state: *mut ThalamusState,
  pub api: &'static ThalamusAPI
}

unsafe extern "C" fn state_on_change(source_raw: *mut ThalamusState, action: i32, key_raw: *mut ThalamusState, value_raw: *mut ThalamusState, data: *mut ::std::os::raw::c_void) {
  unsafe {
    let args = &mut *(data as *mut StateConnectionCallbackArgs);

    let source = State {state: source_raw, api: args.api};
    let key = State {state: key_raw, api: args.api};
    let value = State {state: value_raw, api: args.api};

    let callback = &mut*args.callback;
    callback.on_change(&source, action, &key, &value);
  }
}

unsafe extern "C" fn timer_on_timer(error: *mut ThalamusErrorCode, data: *mut ::std::os::raw::c_void) {
  unsafe {
    let args = &mut *(data as *mut TimerCallbackArgs);

    let error_code = ((&(*args.api)).error_code_value)(error);

    let callback = &mut*args.callback;
    callback.on_timer(ErrorCode {value: error_code});
  }
}

struct StateConnectionCallbackArgs {
  pub callback: *mut dyn StateListener,
  pub api: &'static ThalamusAPI
}

struct TimerCallbackArgs {
  pub callback: *mut dyn TimerListener,
  pub api: *const ThalamusAPI
}

pub struct ErrorCode {
  pub value: i32
}

impl ErrorCode {
  pub fn aborted(&self) -> bool {
    self.value == *OPERATION_ABORTED.get().unwrap()
  }
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
      println!("connect {:?} {:?} {:?}", self.api, connection, raw);
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
    println!("connect drop {:?} {:?} {:?}", self.api, self.connection, self.callback);
    unsafe {
      (self.api.state_recursive_change_disconnect)(self.connection);
      drop(Box::from_raw(self.callback));
    }
    println!("StateConnection::drop");
  }
}

pub struct Timer {
  pub timer: *mut ThalamusTimer,
  pub api: *const ThalamusAPI
}

impl Timer {
  pub fn new(api: *const ThalamusAPI) -> Timer {
    unsafe {
      let timer = ((&(*api)).timer_create)();
      println!("new {:?} {:?}", api, timer);
      Timer {
        api, timer
      }
    }
  }

  pub fn expires_after(&self, duration: Duration) {
    unsafe {
      ((&(*self.api)).timer_expire_after_ns)(self.timer, duration.as_nanos() as usize);
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
      ((&(*self.api)).timer_async_wait)(self.timer, Some(timer_on_timer), void_ptr);
    }
  }
}

impl Drop for Timer {
  fn drop(&mut self) {
    unsafe {
      println!("drop {:?} {:?}", self.api, self.timer);
      ((&(*self.api)).timer_destroy)(self.timer);
    }
  }
}

pub trait Node {
  fn api(&self) -> *const ThalamusAPI;
  fn base(&self) -> *const ThalamusNode;
  fn ready(&self) {
    unsafe {
      ((&(*self.api())).node_ready)(self.base());
    }
  }
  fn time(&self) -> Duration;
  fn new(base:*const ThalamusNode, api: *const ThalamusAPI, state: State) -> Box<Self>;
}

pub trait AnalogNode {
  fn data(
          &self,
          channel: i32,
      ) -> &[f64];

  fn short_data<'a>(
          &self,
          _channel: i32,
      ) -> &'a [i16] {
        panic!("Unimplemented")
      }

  fn int_data<'a>(
          &self,
          _channel: i32,
      ) -> &'a [i32] {
        panic!("Unimplemented")
      }
  fn ulong_data<'a>(
          &self,
          _channel: i32,
      ) -> &'a [u64] {
        panic!("Unimplemented")
      }

  fn num_channels(&self) -> i32;
  fn sample_interval(&self, channel: i32) -> Duration;
  fn name<'a>(
          &self,
          channel: ::std::os::raw::c_int,
      ) -> &'a str;
  fn has_analog_data(&self) -> bool{
        true
      }
  fn is_short_data(&self) -> bool{
        false
      }
  fn is_int_data(&self) -> bool{
        false
      }
  fn is_ulong_data(&self) -> bool{
        false
      }
  fn is_transformed(&self) -> bool {
        false
      }
  fn scale(&self, _channel: i32) -> f64 {
        return 1.0
      }
  fn offset(&self, _channel: i32) -> f64 {
        return 0.0
      }
}

pub trait DontWrap {
  fn wrap_analog(&self, _: &mut ThalamusNode) {}
}

impl <T: crate::api::Node> DontWrap for &T {}

pub trait WrapAnalog {
  fn wrap_analog(&self, c_node: &mut ThalamusNode);
}

impl<T: crate::api::AnalogNode> WrapAnalog for T {
  fn wrap_analog(&self, c_node: &mut ThalamusNode) {
    println!("WrapAnalog");
    c_node.analog = Box::into_raw(Box::new(ThalamusAnalogNode::new()));
    unsafe {
      (*c_node.analog).data = Some(c_node_data::<T>);
      (*c_node.analog).short_data = None;
      (*c_node.analog).int_data = None;
      (*c_node.analog).ulong_data = None;
      (*c_node.analog).num_channels = Some(c_node_num_channels::<T>);
      (*c_node.analog).sample_interval_ns = Some(c_node_sample_interval_ns::<T>);
      (*c_node.analog).name = Some(c_node_name::<T>);
      (*c_node.analog).name_span = Some(c_node_name_span::<T>);
      (*c_node.analog).has_analog_data = Some(c_node_has_analog_data::<T>);
      (*c_node.analog).is_short_data = Some(c_node_is_short_data::<T>);
      (*c_node.analog).is_int_data = Some(c_node_is_int_data::<T>);
      (*c_node.analog).is_ulong_data = Some(c_node_is_ulong_data::<T>);
      (*c_node.analog).is_transformed = Some(c_node_is_transformed::<T>);
      (*c_node.analog).scale = Some(c_node_scale::<T>);
      (*c_node.analog).offset = Some(c_node_offset::<T>);
    }
  }
}

pub static OPERATION_ABORTED: OnceLock<i32> = OnceLock::<i32>::new();

pub fn setup(api_raw: *mut ThalamusAPI) {
  unsafe {
    let api = &*api_raw;
    OPERATION_ABORTED.set((api.error_code_operation_aborted)())
      .expect("Failed to initialize constant: OPERATION_ABORTED");
  }
}

#[macro_export]
macro_rules! export_nodes {
    ( $(($name:literal, $type:ident)),* ) => {

#[allow(unused_imports)]
use $crate::api::{DontWrap, WrapAnalog};

  $(
impl WrappableNode for $type {
  fn wrap(&self, c_node: &mut ThalamusNode) {
    self.wrap_analog(c_node);
  }
}
  )*


#[unsafe(no_mangle)]
pub extern "C" fn get_node_factories(api: *mut ThalamusAPI) -> *const *const ThalamusNodeFactory {
  println!("get_node_factories");
  $crate::api::setup(api);
  let mut vec = Vec::<*const ThalamusNodeFactory>::new();
  $(
    vec.push(ThalamusNodeFactory::new::<$type>($name, api));
  )*
  vec.push(ptr::null_mut());
  let ptr = vec.as_ptr();
  std::mem::forget(vec);
  ptr
}

}
}