use std::sync::Arc;
use std::{cell::RefCell, ptr};
use std::time::Duration;

mod ffi;
pub mod api;
use api::{
  ThalamusNode,
  State,
  OnDrop,
  Timer,
  ThalamusAPI,
  Node,
  AnalogNode,
  TimerListener,
  ErrorCode,
  time,
  WrappableNode,
  ThalamusNodeFactory,
  StateValue
};

use crate::api::StateKey;

struct DemoNode {
  base: *const ThalamusNode,
  state: State,
  state_connection: Option<OnDrop>,
  timer: Timer,
  running: bool,
  start_time: Duration,
  last_time: Duration,
  frequency: f64,
  amplitude: f64,
  samples: Vec<f64>,
  api: *const ThalamusAPI
}

impl AnalogNode for DemoNode {
  fn data(
          &self,
          _channel: i32,
      ) -> &[f64] {
    &self.samples
  }

  fn num_channels(&self) -> i32 { 1 }
  fn sample_interval(&self, _channel: i32) -> Duration {
    Duration::from_millis(1)
  }
  fn name<'a>(
          &self,
          _channel: i32,
      ) -> &'a str {
        "data"
      }
}

impl TimerListener for DemoNode {
  fn on_timer(&mut self, error: ErrorCode) {
    //println!("error {}", error.value);
    if error.aborted() {
      return;
    }
    
    let now = time(self.api);
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

impl DemoNode {
  fn on_change(&mut self, _source: &State, _action: i32, key: StateValue, value: StateValue) {
    println!("DemoNode::on_change {:?} {:?}", key, value);
    let StateValue::String(key_str) = key else {
      return
    };

    match key_str.as_str() {
      "Running" => {
        if StateValue::Bool(true) == value {
          self.running = true;
          self.start_time = time(self.api);
          self.last_time = self.start_time;
          self.timer.expires_after(Duration::from_millis(16));
          self.timer.async_wait(self);
        } else {
          self.running = false;
        }
      },
      "Amplitude" => {
        let StateValue::Float(val) = value else {
          return
        };
        self.amplitude = val;
      },
      "Frequency" => {
        let StateValue::Float(val) = value else {
          return
        };
        self.frequency = val;
      },
      _ => {}
    }
  }
}

#[allow(non_snake_case)]
fn RUNNING() -> StateKey {
  StateKey::String("Running".to_owned())
}

impl Node for DemoNode {
  fn api(&self) -> *const ThalamusAPI {
    return self.api
  }
  fn base(&self) -> *const ThalamusNode {
    return self.base
  }
  fn time(&self) -> Duration {
    self.last_time - Duration::from_millis(1)
  }

  fn new(base: *const ThalamusNode, api: *const ThalamusAPI, state: State) -> Arc<RefCell<Self>> {
    if let StateValue::Bool(b) = state.get(RUNNING()) {
      println!("Running {}", b);
    }

    let result = Arc::new(RefCell::new(DemoNode {
      base,
      state: state.clone(),
      state_connection: None,
      api,
      timer: Timer::new(api),
      running: false,
      start_time: Duration::from_millis(0),
      last_time: Duration::from_millis(0),
      frequency: 0.0,
      amplitude: 0.0,
      samples: Vec::<f64>::new()
    }));

    if let StateValue::Bool(d) = state.get(RUNNING()) {
      println!("Running2 {}", d);
    }

    let change_ref = Arc::downgrade(&result);
    let callback = move |source: &State, action: i32, key: StateValue, value: StateValue| {
      change_ref.upgrade().map(|val| {
        val.borrow_mut().on_change(source, action, key, value);
      });
    };
    {
      let mut temp = result.borrow_mut();
      temp.state_connection = Some(temp.state.connect(callback));
    }
    state.recap();

    result
  }
}

impl Drop for DemoNode {
  fn drop(&mut self) {
    if let StateValue::Bool(d) = self.state.get(RUNNING()) {
      println!("Running3 {}", d);
    }
    self.state.set(RUNNING(), StateValue::Bool(false));
    println!("DemoNode drop");
  }
}

export_nodes!(
  ("EXT_DEMO", DemoNode)
);
