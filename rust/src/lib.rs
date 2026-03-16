use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Arc};
use std::thread::{self, JoinHandle};
use std::{cell::RefCell, ptr};
use std::time::{Duration};

mod ffi;
mod wakers;
pub mod api;
use api::{
  ThalamusNode,
  State,
  OnDrop,
  ThalamusAPI,
  Node,
  AnalogNode,
  WrappableNode,
  ThalamusNodeFactory,
  StateValue
};

use crate::api::{Clock, Sleeper, SleeperWaker, StateKey, run_task};

enum Message {
  Running(bool),
  Frequency(f64),
  Amplitude(f64),
  Samples(Vec<f64>),
  Time(Duration)
}

struct DemoNode {
  state: State,
  state_connection: Option<OnDrop>,
  thread: Option<JoinHandle<()>>,
  node: *const ThalamusNode,
  api: *const ThalamusAPI,
  running: bool,
  frequency: f64,
  amplitude: f64,
  samples: Vec<f64>,
  sleeper: Sleeper,
  input: Option<Receiver<Message>>,
  output: Option<Sender<Message>>,
  time: Duration,
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

fn gen_signal(input: mpsc::Receiver<Message>, output: mpsc::Sender<Message>, waker: SleeperWaker, clock: Clock) {
  let mut running = true;
  let mut amplitude_opt = None;
  let mut frequency_opt = None;
  let mut samples_opt = None;

  let mut last = clock.now();
  let start = last;
  let mut now = last;
  let interval = Duration::from_millis(16);

  while running {
    while now - last < interval {
     let msg = input.recv_timeout(interval - (now - last));
     match msg {
       Ok(Message::Running(val)) => {
         running = val;
       }
       Ok(Message::Amplitude(val)) => {
         amplitude_opt = Some(val);
       }
       Ok(Message::Frequency(val)) => {
         frequency_opt = Some(val);
       }
       Ok(Message::Samples(val)) => {
         samples_opt = Some(val);
       }
       Ok(Message::Time(_)) => { }
       Err(_) => {}
     }
     now = clock.now();
    }
    now = clock.now();

    let Some(amplitude) = amplitude_opt.as_ref() else {
      continue
    };
    let Some(frequency) = frequency_opt.as_ref() else {
      continue
    };
    let Some(mut samples) = samples_opt.take() else {
      continue
    };

    samples.clear();
    while last < now {
      let elapsed = last - start;
      let elapsed_s = elapsed.as_secs_f64();
      samples.push(amplitude*f64::sin(2.0*3.14*frequency*elapsed_s));
      last += Duration::from_millis(1);
    }
    output.send(Message::Time(last - Duration::from_millis(1))).unwrap();
    output.send(Message::Samples(samples)).unwrap();
    waker.wake();
    samples_opt = None;
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

          let (self_output, gen_input) = mpsc::channel::<Message>();
          let (gen_output, self_input) = mpsc::channel::<Message>();

          let waker = self.sleeper.waker();
          let clock = Clock::new(self.api);
          self.thread = Some(thread::spawn(move || gen_signal(gen_input, gen_output, waker, clock)));
          self_output.send(Message::Amplitude(self.amplitude)).unwrap();
          self_output.send(Message::Frequency(self.frequency)).unwrap();
          self_output.send(Message::Samples(std::mem::take(&mut self.samples))).unwrap();

          self.input = Some(self_input);
          self.output = Some(self_output);
        } else {
          self.output.as_ref().map(|o| o.send(Message::Running(false)).unwrap());
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
    return self.node
  }
  fn time(&self) -> Duration {
    self.time
  }

  fn new(base: *const ThalamusNode, api: *const ThalamusAPI, state: State) -> Arc<RefCell<Self>> {
    if let StateValue::Bool(b) = state.get(RUNNING()) {
      println!("Running {}", b);
    }

    let result = Arc::new(RefCell::new(DemoNode {
      thread: None,
      state: state.clone(),
      state_connection: None,
      sleeper: Sleeper::new(api),
      input: None,
      output: None,
      running: false,
      frequency: 0.0,
      amplitude: 0.0,
      samples: Vec::<f64>::new(),
      time: Duration::from_millis(0),
      node: base,
      api
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
 
    {
      let loop_ref= Arc::downgrade(&result);
      run_task(async move {
        loop {
          let sleep_future = {
            let Some(lock_ref) = loop_ref.upgrade() else {
              break
            };
            lock_ref.borrow_mut().sleeper.wait()
          };
          if !sleep_future.await {
            break
          }
          let Some(lock_ref) = loop_ref.upgrade() else {
            break
          };
          
          let message = {
            let this = lock_ref.borrow();
            let input = this.input.as_ref().unwrap();
            input.recv().unwrap()
          };
          match message {
            Message::Samples(mut val) => {
              {
                let mut this = lock_ref.borrow_mut();
                std::mem::swap(&mut val, &mut this.samples);
              }
              lock_ref.borrow().ready();
              {
                let mut this = lock_ref.borrow_mut();
                std::mem::swap(&mut val, &mut this.samples);
              }
              {
                let this = lock_ref.borrow();
                let output = this.output.as_ref().unwrap();
                output.send(Message::Samples(val)).unwrap();
              }
            },
            Message::Time(val) => {
              let mut this = lock_ref.borrow_mut();
              this.time = val;
            },
            _ => {}
          }
        }
      });
    }

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
