use std::ptr;
use std::time::Duration;

mod ffi;
pub mod api;
use api::{
  ThalamusNode,
  State,
  StateConnection,
  Timer,
  ThalamusAPI,
  Node,
  AnalogNode,
  TimerListener,
  ErrorCode,
  time,
  StateListener,
  WrappableNode,
  ThalamusNodeFactory
};

struct DemoNode {
  base: *const ThalamusNode,
  state: State,
  state_connection: Option<StateConnection>,
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
    println!("error {}", error.value);
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
    println!("DemoNode::on_change {}", key_str);

    match key_str {
      "Running" => 
      {
        let val = value.get_bool();
        self.running = val;
        if self.running {
          self.start_time = time(self.api);
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

  fn new(base: *const ThalamusNode, api: *const ThalamusAPI, state: State) -> Box<Self> {
    let mut result = Box::new(DemoNode {
      base,
      state,
      state_connection: None,
      api,
      timer: Timer::new(api),
      running: false,
      start_time: Duration::from_millis(0),
      last_time: Duration::from_millis(0),
      frequency: 0.0,
      amplitude: 0.0,
      samples: Vec::<f64>::new()
    });

    let connection = Some(result.state.connect(result.as_ref()));
    result.state_connection = connection;

    result
  }
}

export_nodes!(
  ("EXT_DEMO", DemoNode)
);
