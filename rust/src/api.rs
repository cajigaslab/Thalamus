
use core::slice;
use std::collections::VecDeque;
use std::ops::Deref;
use std::pin::Pin;
use std::ptr::null;
use std::rc::Rc;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll, Waker};
use std::{os::raw::c_void, sync::OnceLock};
use std::time::Duration;
use std::ffi::{CStr, CString};

pub use crate::ffi::{
  *
};
use crate::wakers::{self, RcWake};

struct PostArgs<T> {
  call: T
}

unsafe extern "C" fn post_callback<T: FnMut()>(data: *mut ::std::os::raw::c_void) {
  let mut args = unsafe {
    let raw_args = &mut*(data as *mut PostArgs<T>);
    Box::from_raw(raw_args)
  };
  (args.call)();
}

#[derive(Debug,PartialEq,Copy,Clone)]
pub struct ThalamusAPI {
  pub raw: *mut ThalamusAPIRaw,
  pub node: *mut ThalamusNode
}
unsafe impl Send for ThalamusAPI {}

impl ThalamusAPI {
  pub fn time(&self) -> Duration {
    unsafe {
      let time_ns = (&*self.raw).time_ns;
      return Duration::from_nanos(time_ns());
    }
  }

  pub fn ready(&self) {
    unsafe {
      let node_ready = (&*self.raw).node_ready;
      node_ready(self.node);
    }
  }

  pub fn post<T: FnMut() + 'static>(&self, call: T) {
    unsafe {
      let api = &*self.raw;
      let call_ptr = Box::into_raw(Box::new(PostArgs {
        call
      }));
      let void_ptr = call_ptr as *mut std::os::raw::c_void;
      (api.io_context_post)(Some(post_callback::<T>), void_ptr);

      //let call_ref =  &*call_ptr;
      //let mut done = call_ref.mutex.lock().unwrap();
      //while !*done {
      //  done = call_ref.cond.wait(done).unwrap();
      //}
      //
      //drop(Box::from_raw(call_ptr));
    }
  }

  pub fn create_serial_port(&self) -> SerialPort {
    unsafe {
      let api = &*self.raw;
      let port = (api.serial_port_create)();
      SerialPort {
        api: *self, port
      }
    }
  }

  pub fn create_streambuf(&self) -> StreamBuf {
    unsafe {
      let api = &*self.raw;
      let buffer = (api.streambuf_create)();
      StreamBuf {
        api: *self, buffer
      }
    }
  }
}

struct SleeperState {
  wakes: i32,
  futures: VecDeque<Arc<Mutex<SleeperFutureState>>>
}

pub struct Sleeper {
  api: ThalamusAPI,
  state: Arc<Mutex<SleeperState>>
}

pub struct SleeperWaker {
  api: ThalamusAPI,
  state: Arc<Mutex<SleeperState>>
}

struct SleeperFutureState {
  state: Arc<Mutex<SleeperState>>,
  waker: Option<Waker>
}

pub struct SleeperFuture {
  state: Arc<Mutex<SleeperFutureState>>
}

impl Future for SleeperFuture {
  type Output = bool;

  fn poll(self: std::pin::Pin<&mut Self>, cx: &mut std::task::Context<'_>) -> std::task::Poll<Self::Output> {
    let mut future_state = self.state.lock().unwrap();
    let mut state = future_state.state.lock().unwrap();
    if state.wakes > 0 {
      state.wakes -= 1;
      std::task::Poll::Ready(true)
    } else if state.wakes < 0 {
      std::task::Poll::Ready(false)
    } else {
      drop(state);
      match &mut future_state.waker {
        Some(waker) => {
          if !waker.will_wake(cx.waker()) {
            *waker = cx.waker().clone();
          }
        },
        None => {
          future_state.waker = Some(cx.waker().clone());
        }
      }
      std::task::Poll::Pending
    }
  }
}

impl SleeperWaker {
  pub fn wake_impl(&self, immediate: bool) {
    let state = self.state.clone();
    let closure = move || { 
      let future = {
        let mut lock = state.lock().unwrap();
        if lock.wakes >= 0 {
          lock.wakes += 1;
        }
        lock.futures.pop_front()
      };
      future.map(|f| {
        let waker = f.lock().unwrap().waker.take();
        waker.as_ref().map(|w| w.wake_by_ref());
      });
    };
    if immediate {
      closure();
    } else {
      self.api.post(closure);
    }
  }

  pub fn wake(&self) {
    self.wake_impl(false);
  }
}

impl Sleeper {
  pub fn new(api: ThalamusAPI) -> Sleeper {
    Sleeper { api, state: Arc::new(Mutex::new( SleeperState { wakes: 0, futures: VecDeque::<Arc<Mutex<SleeperFutureState>>>::new() }))}
  }

  pub fn waker(&self) -> SleeperWaker {
    SleeperWaker { api: self.api, state: self.state.clone() }
  }

  pub fn wait(&self) -> SleeperFuture {
    let mut lock = self.state.lock().unwrap();

    let result = SleeperFuture { state: Arc::new(Mutex::new(SleeperFutureState {state: self.state.clone(), waker: None })) };
    lock.futures.push_back(result.state.clone());
    result
  }
}

impl Drop for Sleeper {
  fn drop(&mut self) {
    let mut state = self.state.lock().unwrap();
    state.wakes = -1;
    while !state.futures.is_empty() {
      let waker = self.waker();
      drop(state);
      waker.wake_impl(true);
      state = self.state.lock().unwrap();
    }
  }
}

struct IOArgs<T> {
  api: ThalamusAPI,
  callback: T
}

unsafe extern "C" fn io_callback<T: FnMut(ErrorCode, usize)>(error: *mut ThalamusErrorCode, length: usize, data: *mut ::std::os::raw::c_void) {
  let mut args = unsafe {
    let raw_args = &mut*(data as *mut IOArgs<T>);
    Box::from_raw(raw_args)
  };
  let error_code = unsafe {
    let api = &*args.api.raw;
    (api.error_code_value)(error)
  };
  
  (args.callback)(ErrorCode{value: error_code}, length);
}

pub struct IOFutureState {
  waker: Option<Waker>,
  result: Option<Result<usize, ErrorCode>>
  //state: Arc<Mutex<SleeperFutureState>>
}

pub struct IOFuture {
  state: Arc<Mutex<IOFutureState>>
}

impl Future for IOFuture {
  type Output = Result<usize, ErrorCode>;

  fn poll(self: std::pin::Pin<&mut Self>, cx: &mut std::task::Context<'_>) -> std::task::Poll<Self::Output> {
    let mut state = self.state.lock().unwrap();
    match state.result.take() {
      Some(result) => {
        std::task::Poll::Ready(result)
      },
      None => {
        match &mut state.waker {
          Some(waker) => {
            if !waker.will_wake(cx.waker()) {
              *waker = cx.waker().clone();
            }
          },
          None => {
            state.waker = Some(cx.waker().clone());
          }
        }
        std::task::Poll::Pending
      }
    }
  }
}

pub struct StreamBuf {
  api: ThalamusAPI,
  buffer: *mut ThalamusStreamBuf
}

impl Drop for StreamBuf {
  fn drop(&mut self) {
    unsafe {
      let api = &*self.api.raw;
      (api.streambuf_destroy)(self.buffer);
    }
  }
}

impl StreamBuf {
  pub fn to_string(&self) -> String {
    unsafe {
      let api = &*self.api.raw;
      let mut span = ThalamusCharSpan { data: null(), size: 0, owns_data: 0};
      (api.streambuf_to_span)(&mut span as *mut ThalamusCharSpan, self.buffer);
      let slice = slice::from_raw_parts(span.data as *mut u8, span.size);
      let text = str::from_utf8(slice).unwrap();
      let result = text.to_string();
      (api.charspan_destroy)(&mut span as *mut ThalamusCharSpan);
      result
    }
  }

  pub fn consume(&self, count: usize) {
    unsafe {
      let api = &*self.api.raw;
      (api.streambuf_consume)(self.buffer, count);
    }
  }

  pub fn size(&self) -> usize {
    unsafe {
      let api = &*self.api.raw;
      (api.streambuf_size)(self.buffer)
    }
  }
}

pub struct SerialPort {
  api: ThalamusAPI,
  port: *mut ThalamusSerialPort
}

impl SerialPort {
  pub fn error(&self) -> Option<ErrorCode> {
    unsafe {
      let api = &*self.api.raw;
      let error = (api.serial_port_error)(self.port);
      let error_code = (api.error_code_value)(error);
      if error_code == 0 {
        None
      } else {
        Some(ErrorCode {value: error_code})
      }
    }
  }
  pub fn open(&self, name: &str) -> Result<(), ErrorCode> {
    unsafe {
      let api = &*self.api.raw;
      let c_str= CString::new(name).unwrap();
      (api.serial_port_open)(self.port, c_str.as_ptr());
      match self.error() {
        None => Ok(()),
        Some(error) => Err(error)
      }
    }
  }
  pub fn set_baud_rate(&self, rate: u32) -> Result<(), ErrorCode> {
    unsafe {
      let api = &*self.api.raw;
      (api.serial_set_baud_rate)(self.port, rate);
      match self.error() {
        None => Ok(()),
        Some(error) => Err(error)
      }
    }
  }

  pub fn write_callback<T: FnMut(ErrorCode, usize)>(&self, data: &[u8], callback: T) {
    unsafe {
      let api = &*self.api.raw;
      let boxed = Box::new(IOArgs {
        api: self.api,
        callback
      });
      let args = Box::into_raw(boxed)  as *mut std::os::raw::c_void;
      let mut span = ThalamusByteSpan { data: data.as_ptr(), size: data.len() };
      (api.serial_port_write)(self.port, &mut span as *mut ThalamusByteSpan, Some(io_callback::<T>), args);
    }
  }
  pub fn write(&self, data: &[u8]) -> IOFuture {
    let future = IOFuture {state: Arc::new(Mutex::new(IOFutureState {result: None, waker: None}))};
    self.write_callback(data, |error, size| {
      let waker = {
        let mut state = future.state.lock().unwrap();
        state.result = if error.value != 0 { Some(Err(error)) } else { Some(Ok(size)) };
        state.waker.take()
      };
      waker.map(|w| { w.wake_by_ref();});
    });
    future
  }

  pub fn read_callback<T: FnMut(ErrorCode, usize)>(&self, data: &mut [u8], callback: T) {
    unsafe {
      let api = &*self.api.raw;
      let boxed = Box::new(IOArgs {
        api: self.api,
        callback
      });
      let args = Box::into_raw(boxed)  as *mut std::os::raw::c_void;
      let mut span = ThalamusByteSpan { data: data.as_ptr(), size: data.len() };
      (api.serial_port_read)(self.port, &mut span as *mut ThalamusByteSpan, Some(io_callback::<T>), args);
    }
  }
  pub fn read(&self, data: &mut [u8]) -> IOFuture {
    let future = IOFuture {state: Arc::new(Mutex::new(IOFutureState {result: None, waker: None}))};
    self.read_callback(data, |error, size| {
      let waker = {
        let mut state = future.state.lock().unwrap();
        state.result = if error.value != 0 { Some(Err(error)) } else { Some(Ok(size)) };
        state.waker.take()
      };
      waker.map(|w| { w.wake_by_ref();});
    });
    future
  }

  pub fn read_some_callback<T: FnMut(ErrorCode, usize)>(&self, data: &mut [u8], callback: T) {
    unsafe {
      let api = &*self.api.raw;
      let boxed = Box::new(IOArgs {
        api: self.api,
        callback
      });
      let args = Box::into_raw(boxed)  as *mut std::os::raw::c_void;
      let mut span = ThalamusByteSpan { data: data.as_ptr(), size: data.len() };
      (api.serial_port_read_some)(self.port, &mut span as *mut ThalamusByteSpan, Some(io_callback::<T>), args);
    }
  }
  pub fn read_some(&self, data: &mut [u8]) -> IOFuture {
    let future = IOFuture {state: Arc::new(Mutex::new(IOFutureState {result: None, waker: None}))};
    self.read_some_callback(data, |error, size| {
      let waker = {
        let mut state = future.state.lock().unwrap();
        state.result = if error.value != 0 { Some(Err(error)) } else { Some(Ok(size)) };
        state.waker.take()
      };
      waker.map(|w| { w.wake_by_ref();});
    });
    future
  }

  pub fn read_until_callback<T: FnMut(ErrorCode, usize)>(&self, buffer: &StreamBuf, delimiter: &str, callback: T) {
    unsafe {
      let api = &*self.api.raw;
      let boxed = Box::new(IOArgs {
        api: self.api,
        callback
      });
      let args = Box::into_raw(boxed)  as *mut std::os::raw::c_void;
      let delimiter_bytes = delimiter.as_bytes();
      let delimiter_ptr = delimiter_bytes.as_ptr() as *const i8;
      (api.serial_port_read_until)(self.port, buffer.buffer, delimiter_ptr, delimiter_bytes.len(), Some(io_callback::<T>), args);
    }
  }
  pub fn read_until(&self, buffer: &StreamBuf, delimiter: &str) -> IOFuture {
    let future = IOFuture {state: Arc::new(Mutex::new(IOFutureState {result: None, waker: None}))};
    self.read_until_callback(buffer, delimiter, |error, size| {
      let waker = {
        let mut state = future.state.lock().unwrap();
        state.result = if error.value != 0 { Some(Err(error)) } else { Some(Ok(size)) };
        state.waker.take()
      };
      waker.map(|w| { w.wake_by_ref();});
    });
    future
  }
}

impl Drop for SerialPort {
  fn drop(&mut self) {
    unsafe {
      let api = &*self.api.raw;
      (api.serial_port_destroy)(self.port);
    }
  }
}

struct TaskState {
  future: Option<Pin<Box<dyn Future<Output = ()>>>>,
  poll: Poll<()>
}

impl TaskState {
  fn poll(&mut self, cx: &mut Context<'_>) {
    match self.future.as_mut() {
      None => {},
      Some(pin_future) => {
        let future = pin_future.as_mut();
        if self.poll.is_pending() {
          self.poll = future.poll(cx);
        }
      }
    }
  }
}

struct Task {
  state: Mutex<TaskState>
}

impl Task {
  fn poll(self: &Rc<Self>) {
    let mut state = self.state.lock().unwrap();
    if let None = state.future {
      return;
    }
    let waker = wakers::waker(self.clone());
    let mut cx = Context::from_waker(&waker);
    state.poll(&mut cx);
  }
}

impl RcWake for Task {
  fn wake_by_ref(arc_self: &Rc<Self>) {
    arc_self.poll();
  }
}

pub struct TaskScope {
  task: Rc<Task>
}

impl Drop for TaskScope {
  fn drop(&mut self) {
    let mut state = self.task.state.lock().unwrap();
    state.future = None;
  }
}

pub fn run_task<F>(future: F) -> TaskScope
where
  F: Future<Output = ()> + 'static
{
  let task = Rc::new(Task {
    state: Mutex::new(TaskState {
    future: Some(Box::pin(future)), poll: Poll::Pending})
  });

  task.poll();
  TaskScope{ task: task.clone() }
}

pub fn time(api: *const ThalamusAPIRaw) -> Duration {
  unsafe {
    return Duration::from_nanos(((&*api).time_ns)());
  }
}

#[derive(Debug,PartialEq)]
pub struct State {
  state: *mut ThalamusState,
  api: ThalamusAPI
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

fn wrap_state(api:ThalamusAPI, arg: *mut ThalamusState) -> StateValue {
  unsafe {
    let raw = &*api.raw;
    if (raw.state_is_bool)(arg) != 0 {
      StateValue::Bool((raw.state_get_bool)(arg) != 0)
    } else if (raw.state_is_dict)(arg) != 0 {
      StateValue::Dict(State::new(api, arg))
    } else if (raw.state_is_float)(arg) != 0 {
      StateValue::Float((raw.state_get_float)(arg))
    } else if (raw.state_is_int)(arg) != 0 {
      StateValue::Int((raw.state_get_int)(arg))
    } else if (raw.state_is_list)(arg) != 0 {
      StateValue::List(State::new(api, arg))
    } else if (raw.state_is_string)(arg) != 0 {
      let ptr = ((&*raw).state_get_string)(arg);
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
  pub api: *const ThalamusAPIRaw
}

#[derive(Debug)]
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
  pub api: ThalamusAPI
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
  pub fn new(api:ThalamusAPI, state:*mut ThalamusState) -> State {
    unsafe {
      ((&*api.raw).state_inc_ref)(state);
    }
    State {
      state: state, api: api
    }
  }

  pub fn get(&self, index: StateKey) -> StateValue {
    let result = match index {
      StateKey::Int(key) => {
        unsafe {
          ((&*self.api.raw).state_get_at_index)(self.state, key as usize)
        }
      },
      StateKey::String(key_raw) => {
        let key = CString::new(key_raw).unwrap();
        unsafe {
          ((&*self.api.raw).state_get_at_name)(self.state, key.as_ptr())
        }
      }
    };
    wrap_state(self.api, result)
  }

  pub fn set(&self, index: StateKey, raw_value: StateValue) {
    let api = unsafe { &*self.api.raw };
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
      ((&*self.api.raw).state_recap)(self.state);
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
        ((&*self.api.raw).state_recursive_change_connect)(self.state, Some(state_on_change::<T>), raw as *mut c_void)
      };
      println!("connect {:?} {:?} {:?}", self.api, connection, raw);

      let cleanup_api = self.api;
      let cleanup = move || {
        println!("connect drop {:?} {:?}", cleanup_api, connection);
        unsafe {
          ((&*cleanup_api.raw).state_recursive_change_disconnect)(connection);
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
      ((&*self.api.raw).state_dec_ref)(self.state);
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
  pub api: *const ThalamusAPIRaw
}

impl Timer {
  pub fn new(api: *const ThalamusAPIRaw) -> Timer {
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
  fn time(&self) -> Duration;
  fn new(api: ThalamusAPI, state: State) -> Self;
}

//impl<'a, REF, VAL: ?Sized, FUNC: Fn(&Ref<'a, REF>) -> &'a VAL> RefCellGuard<'a, REF, VAL, FUNC> {
//  pub fn new(_ref: Ref<'a, REF>, val:&'a VAL) -> RefCellGuard<'a, REF, VAL> {
//    RefCellGuard::<'a> {
//      _ref, val
//    }
//  }
//}

pub struct SliceDeref<T: Deref<Target = Vec<f64>>> {
  inner: T
}

impl<T: Deref<Target = Vec<f64>>> SliceDeref<T> {
  pub fn new(inner: T) -> SliceDeref<T> {
    SliceDeref { inner }
  }
}

impl<T: Deref<Target = Vec<f64>>> Deref for SliceDeref<T> {
    type Target = [f64];

    fn deref(&self) -> &Self::Target {
        self.inner.as_slice()
    }
}

pub trait AnalogNode {
  fn data(
          &self,
          channel: i32,
      ) -> impl Deref<Target = [f64]>;

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
  fn name(
          &self,
          channel: ::std::os::raw::c_int,
      ) -> &str;
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

pub fn setup(api_raw: *mut ThalamusAPIRaw) {
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
pub extern "C" fn get_node_factories(api: *mut ThalamusAPIRaw) -> *const *const ThalamusNodeFactory {
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