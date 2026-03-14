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
  StateListener,
  WrappableNode,
  ThalamusNodeFactory,
  DictSetter
};

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

impl StateListener for DemoNode {
  fn on_change(&mut self, _source: &State, _action: i32, key: &State, value: &State) {
    let key_str = key.get_string();
    print!("DemoNode::on_change {}", key_str);

    match key_str {
      "Running" => 
      {
        let val = value.get_bool();
        self.running = val;
        println!(" {}", val);
        if self.running {
          self.start_time = time(self.api);
          self.last_time = self.start_time;
          self.timer.expires_after(Duration::from_millis(16));
          self.timer.async_wait(self);
        }
      },
      "Amplitude" => {
        let val = value.get_float();
        println!(" {}", val);
        self.amplitude = val;
      },
      "Frequency" => {
        let val = value.get_float();
        println!(" {}", val);
        self.frequency = val;
      },
      _ => {
        println!("")
      }
    }
  }
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
    let a = state.get_dict_value("Running");
    let b = a.get_bool();
    println!("Running {}", b);

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

    let c = result.borrow().state.get_dict_value("Running");
    let d = c.get_bool();
    println!("Running2 {}", d);

    let change_ref = Arc::downgrade(&result);
    let callback = move |source: &State, action: i32, key: &State, value: &State| {
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
    let c = self.state.get_dict_value("Running");
    let d = c.get_bool();
    println!("Running3 {}", d);
    self.state.set_dict_bool("Running", false);
    println!("DemoNode drop");
  }
}

export_nodes!(
  ("EXT_DEMO", DemoNode)
);
