
use std::cell::RefCell;
use std::sync::Arc;
use std::{os::raw::c_void, sync::OnceLock};
use std::time::Duration;
use std::ffi::{CStr, CString};

pub use crate::ffi::{
  *
};

pub fn time(api: *const ThalamusAPI) -> Duration {
  unsafe {
    return Duration::from_nanos(((&*api).time_ns)());
  }
}

#[derive(Debug,PartialEq)]
pub struct State {
  state: *mut ThalamusState,
  api: *const ThalamusAPI
}

#[derive(Debug,PartialEq)]
pub enum StateValue {
  Bool(bool),
  Dict(State),
  Float(f64),
  Int(i64),
  List(State),
  String(String),
  Null
}

pub enum StateKey {
  Int(i64),
  String(String)
}

fn wrap_state(api_raw:*const ThalamusAPI, arg: *mut ThalamusState) -> StateValue {
  unsafe {
    let api = &*api_raw;
    if (api.state_is_bool)(arg) != 0 {
      StateValue::Bool((api.state_get_bool)(arg) != 0)
    } else if (api.state_is_dict)(arg) != 0 {
      StateValue::Dict(State::new(api, arg))
    } else if (api.state_is_float)(arg) != 0 {
      StateValue::Float((api.state_get_float)(arg))
    } else if (api.state_is_int)(arg) != 0 {
      StateValue::Int((api.state_get_int)(arg))
    } else if (api.state_is_list)(arg) != 0 {
      StateValue::List(State::new(api, arg))
    } else if (api.state_is_string)(arg) != 0 {
      let ptr = ((&*api).state_get_string)(arg);
      let text = CStr::from_ptr(ptr).to_str().unwrap();
      StateValue::String(text.to_string())
    } else {
      StateValue::Null
    }
  }
}

unsafe extern "C" fn state_on_change<T: FnMut(&State, i32, StateValue, StateValue)>(source_raw: *mut ThalamusState, action: i32, key_raw: *mut ThalamusState, value_raw: *mut ThalamusState, data: *mut ::std::os::raw::c_void) {
  let args = unsafe { &mut *(data as *mut StateConnectionCallbackArgs<T>) };

  let key = wrap_state(args.api, key_raw);
  let value = wrap_state(args.api, value_raw);
  let source = State {state: source_raw, api: args.api};
  
  (args.callback)(&source, action, key, value);
}

unsafe extern "C" fn timer_on_timer(error: *mut ThalamusErrorCode, data: *mut ::std::os::raw::c_void) {
  unsafe {
    let args = &mut *(data as *mut TimerCallbackArgs);

    let error_code = ((&(*args.api)).error_code_value)(error);

    let callback = &mut*args.callback;
    callback.on_timer(ErrorCode {value: error_code});
  }
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

struct StateConnectionCallbackArgs<T: FnMut(&State, i32, StateValue, StateValue)> {
  pub callback: T,
  pub api: *const ThalamusAPI
}

pub trait TimerListener {
    fn on_timer(&mut self, error: ErrorCode);
}

pub trait DictSetter {
  fn set_dict_state(&self, key_raw: &str, value: &State);
  fn set_dict_str(&self, key_raw: &str, value_raw: &str);
  fn set_dict_int(&self, key_raw: &str, value_raw: i64);
  fn set_dict_float(&self, key_raw: &str, value_raw: f64);
  fn set_dict_null(&self, key_raw: &str);
  fn set_dict_bool(&self, key_raw: &str, value: bool);
}

pub trait ListSetter {
  fn set_list_state(&self, key_raw: i64, value: &State);
  fn set_list_str(&self, key_raw: i64, value_raw: &str);
  fn set_list_int(&self, key_raw: i64, value_raw: i64);
  fn set_list_float(&self, key_raw: i64, value_raw: f64);
  fn set_list_null(&self, key_raw: i64);
  fn set_list_bool(&self, key_raw: i64, value: bool);
}

impl State {
  pub fn new(api:*const ThalamusAPI, state:*mut ThalamusState) -> State {
    unsafe {
      ((&*api).state_inc_ref)(state);
    }
    State {
      state: state, api: api
    }
  }

  pub fn get(&self, index: StateKey) -> StateValue {
    let result = match index {
      StateKey::Int(key) => {
        unsafe {
          ((&*self.api).state_get_at_index)(self.state, key as usize)
        }
      },
      StateKey::String(key_raw) => {
        let key = CString::new(key_raw).unwrap();
        unsafe {
          ((&*self.api).state_get_at_name)(self.state, key.as_ptr())
        }
      }
    };
    wrap_state(self.api, result)
  }

  pub fn set(&self, index: StateKey, raw_value: StateValue) {
    let api = unsafe { &*self.api };
    match index {
      StateKey::Int(key) => {
        unsafe {
          match raw_value {
            StateValue::Bool(value) => {
              (api.state_set_at_index_bool)(self.state, key, if value { 1 } else { 0 });
            },
            StateValue::Dict(value) => {
              (api.state_set_at_index_state)(self.state, key, value.state);
            },
            StateValue::Float(value) => {
              (api.state_set_at_index_float)(self.state, key, value);
            },
            StateValue::Int(value) => {
              (api.state_set_at_index_int)(self.state, key, value);
            },
            StateValue::List(value) => {
              (api.state_set_at_index_state)(self.state, key, value.state);
            },
            StateValue::String(rust_value) => {
              let value = CString::new(rust_value).unwrap();
              (api.state_set_at_index_string)(self.state, key, value.as_ptr());
            },
            StateValue::Null => {
              (api.state_set_at_index_null)(self.state, key);
            },
          };
        }
      },
      StateKey::String(key_raw) => {
        let key_str = CString::new(key_raw).unwrap();
        let key = key_str.as_ptr();
        unsafe {
          match raw_value {
            StateValue::Bool(value) => {
              (api.state_set_at_name_bool)(self.state, key, if value { 1 } else { 0 });
            },
            StateValue::Dict(value) => {
              (api.state_set_at_name_state)(self.state, key, value.state);
            },
            StateValue::Float(value) => {
              (api.state_set_at_name_float)(self.state, key, value);
            },
            StateValue::Int(value) => {
              (api.state_set_at_name_int)(self.state, key, value);
            },
            StateValue::List(value) => {
              (api.state_set_at_name_state)(self.state, key, value.state);
            },
            StateValue::String(rust_value) => {
              let value = CString::new(rust_value).unwrap();
              (api.state_set_at_name_string)(self.state, key, value.as_ptr());
            },
            StateValue::Null => {
              (api.state_set_at_name_null)(self.state, key);
            },
          };
        }
      }
    };
  }

  pub fn recap(&self) {
    unsafe {
      ((&*self.api).state_recap)(self.state);
    }
  }

  pub fn connect<'a, T: FnMut(&State, i32, StateValue, StateValue) + 'static>(&self, callback: T) -> OnDrop
  {
      let callback_args = Box::new(StateConnectionCallbackArgs {
        callback,
        api: self.api
      });
      let raw = Box::into_raw(callback_args);
      let connection = unsafe {
        ((&*self.api).state_recursive_change_connect)(self.state, Some(state_on_change::<T>), raw as *mut c_void)
      };
      println!("connect {:?} {:?} {:?}", self.api, connection, raw);

      let cleanup_api = self.api;
      let cleanup = move || {
        println!("connect drop {:?} {:?}", cleanup_api, connection);
        unsafe {
          ((&*cleanup_api).state_recursive_change_disconnect)(connection);
          drop(Box::from_raw(raw));
        }
        println!("StateConnection::drop");
      };

      OnDrop { action: Box::new(cleanup) }
  }
}

impl Clone for State {
  fn clone(&self) -> Self {
    State::new(self.api, self.state)
  }
}

impl Drop for State {
  fn drop(&mut self) {
    unsafe {
      ((&*self.api).state_dec_ref)(self.state);
    }
  }
}

pub struct OnDrop {
  action: Box<dyn FnMut()>
}

impl Drop for OnDrop {
  fn drop(&mut self) {
    (self.action)();
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
  fn new(base:*const ThalamusNode, api: *const ThalamusAPI, state: State) -> Arc<RefCell<Self>>;
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