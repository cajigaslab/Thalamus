use std::ops::Deref;
use std::rc::Rc;
use std::sync::mpsc::{self, Receiver, Sender};
use std::thread::{self, JoinHandle, sleep};
use std::{cell::RefCell, ptr};
use std::time::{Duration};

mod ffi;
mod wakers;
pub mod api;
use api::{
  ThalamusNode,
  State,
  OnDrop,
  ThalamusAPIRaw,
  Node,
  AnalogNode,
  WrappableNode,
  ThalamusNodeFactory,
  StateValue
};
use regex::Regex;

use crate::api::{OPERATION_ABORTED, Sleeper, SleeperWaker, SliceDeref, StateKey, TaskScope, ThalamusAPI, run_task};

enum Message {
  Running(bool),
  Frequency(f64),
  Amplitude(f64),
  Samples(Vec<f64>),
  Time(Duration)
}

struct DemoNodeInner {
  state: State,
  state_connection: RefCell<Option<OnDrop>>,
  thread: RefCell<Option<JoinHandle<()>>>,
  api: ThalamusAPI,
  frequency: RefCell<f64>,
  amplitude: RefCell<f64>,
  samples: RefCell<Vec<f64>>,
  sleeper: Sleeper,
  input: RefCell<Option<Receiver<Message>>>,
  output: RefCell<Option<Sender<Message>>>,
  time: RefCell<Duration>,
  task: RefCell<Option<TaskScope>>
}

struct DemoNode {
  inner: Rc<DemoNodeInner>
}

impl AnalogNode for DemoNode {
  fn data(
          &self,
          _channel: i32,
      ) -> impl Deref<Target = [f64]> {
    SliceDeref::new(self.inner.samples.borrow())
  }

  fn num_channels(&self) -> i32 { 1 }
  fn sample_interval(&self, _channel: i32) -> Duration {
    Duration::from_millis(1)
  }
  fn name(
          &self,
          _channel: i32,
      ) -> &str {
        "data"
      }
}

fn gen_signal(input: mpsc::Receiver<Message>, output: mpsc::Sender<Message>, waker: SleeperWaker, api: ThalamusAPI) {
  let mut running = true;
  let mut amplitude_opt = None;
  let mut frequency_opt = None;
  let mut samples_opt = None;

  let mut last = api.time();
  let start = last;
  let mut now = last;
  let interval = Duration::from_millis(16);

  while running {
    let elapsed = now - last;
    let poll_time = if elapsed < interval { interval - elapsed } else { Duration::from_millis(0) };
    let msg = input.recv_timeout(poll_time);
    match msg {
      Ok(Message::Running(val)) => {
        running = val;
        continue;
      }
      Ok(Message::Amplitude(val)) => {
        amplitude_opt = Some(val);
        continue;
      }
      Ok(Message::Frequency(val)) => {
        frequency_opt = Some(val);
        continue;
      }
      Ok(Message::Samples(val)) => {
        samples_opt = Some(val);
        continue;
      }
      Ok(Message::Time(_)) => { }
      Err(_) => {}
    }
    now = api.time();

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
    waker.wake();
    output.send(Message::Samples(samples)).unwrap();
    waker.wake();

    if now < last {
      sleep(last - now);
      now = api.time();
    }
  }
}

impl DemoNodeInner {
  fn on_change(&self, _source: &State, _action: i32, key: StateValue, value: StateValue) {
    println!("DemoNode::on_change {:?} {:?}", key, value);
    let StateValue::String(key_str) = key else {
      return
    };

    match key_str.as_str() {
      "Running" => {
        if StateValue::Bool(true) == value {
          let (self_output, gen_input) = mpsc::channel::<Message>();
          let (gen_output, self_input) = mpsc::channel::<Message>();

          let waker = self.sleeper.waker();
          let api = self.api;
          *self.thread.borrow_mut() = Some(thread::Builder::new()
                                             .name("gen_signal".into())
                                             .spawn(move || gen_signal(gen_input, gen_output, waker, api)).expect("Failed to spawn thread"));
          self_output.send(Message::Amplitude(*self.amplitude.borrow())).unwrap();
          self_output.send(Message::Frequency(*self.frequency.borrow())).unwrap();
          self_output.send(Message::Samples(std::mem::take(&mut *self.samples.borrow_mut()))).unwrap();

          *self.input.borrow_mut() = Some(self_input);
          *self.output.borrow_mut() = Some(self_output);
        } else if let Some(thread) = self.thread.take() {
          self.output.borrow().as_ref().map(|o| o.send(Message::Running(false)).unwrap());
          thread.join().expect("Thread join failed");
        }
      },
      "Amplitude" => {
        let StateValue::Float(val) = value else {
          return
        };
        *self.amplitude.borrow_mut() = val;
        if let Some(out) = &*self.output.borrow() {
          out.send(Message::Amplitude(val)).expect("Send Amplitude failed");
        }
      },
      "Frequency" => {
        let StateValue::Float(val) = value else {
          return
        };
        *self.frequency.borrow_mut() = val;
        if let Some(out) = &*self.output.borrow() {
          out.send(Message::Frequency(val)).expect("Send Frequency failed");
        }
      },
      _ => {}
    }
  }
}

impl DemoNode {
}

#[allow(non_snake_case)]
fn RUNNING() -> StateKey {
  StateKey::String("Running".to_owned())
}

impl Node for DemoNode {
  fn time(&self) -> Duration {
    *self.inner.time.borrow()
  }

  fn new(api: ThalamusAPI, state: State) -> Self {
    let inner = Rc::new(DemoNodeInner {
      thread: RefCell::new(None),
      state: state.clone(),
      state_connection: RefCell::new(None),
      sleeper: Sleeper::new(api),
      input: RefCell::new(None),
      output: RefCell::new(None),
      frequency: RefCell::new(0.0),
      amplitude: RefCell::new(0.0),
      samples: RefCell::new(Vec::new()),
      time: RefCell::new(Duration::from_millis(0)),
      api,
      task: RefCell::new(None)
    });

    let change_ref = Rc::downgrade(&inner);
    let callback = move |source: &State, action: i32, key: StateValue, value: StateValue| {
      change_ref.upgrade().map(|val| {
        val.on_change(source, action, key, value);
      });
    };
    {
      let mut temp = inner.state_connection.borrow_mut();
      *temp = Some(inner.state.connect(callback));
    }
    state.recap();
 
    {
      let loop_ref= Rc::downgrade(&inner);
      *inner.task.borrow_mut() = Some(run_task(async move {
        loop {
          let sleep_future = {
            let Some(lock_ref) = loop_ref.upgrade() else {
              break
            };
            lock_ref.sleeper.wait()
          };
          if !sleep_future.await {
            break
          }
          let Some(lock_ref) = loop_ref.upgrade() else {
            break
          };
          
          let message = {
            lock_ref.input.borrow().as_ref().unwrap().recv().unwrap()
          };
          match message {
            Message::Samples(mut val) => {
              {
                let mut samples = lock_ref.samples.borrow_mut();
                std::mem::swap(&mut val, &mut *samples);
              }
              api.ready();
              {
                let mut samples = lock_ref.samples.borrow_mut();
                std::mem::swap(&mut val, &mut *samples);
              }
              {
                lock_ref.output.borrow().as_ref().unwrap().send(Message::Samples(val)).unwrap();
              }
            },
            Message::Time(val) => {
              let mut time = lock_ref.time.borrow_mut();
              *time = val;
            },
            _ => {}
          };
        }
      }));
    }

    DemoNode { inner }
  }
}

impl Drop for DemoNode {
  fn drop(&mut self) {
    if let StateValue::Bool(d) = self.inner.state.get(RUNNING()) {
      println!("Running3 {}", d);
    }
    self.inner.state.set(RUNNING(), StateValue::Bool(false));
    if let Some(thread) = self.inner.thread.take() {
      self.inner.output.borrow().as_ref().map(|o| o.send(Message::Running(false)).unwrap());
      thread.join().expect("Thread join failed");
    }
    println!("DemoNode drop");
  }
}

struct SerialNodeInner {
  state: State,
  state_connection: RefCell<Option<OnDrop>>,
  api: ThalamusAPI,
  samples: RefCell<Vec<f64>>,
  time: RefCell<Duration>,
  port: RefCell<String>,
  task: RefCell<Option<TaskScope>>
}

impl SerialNodeInner {
  async fn serial_loop(&self) {
    let port = self.api.create_serial_port();
    port.open(&self.port.borrow()).unwrap();
    port.set_baud_rate(115200).unwrap();

    let buffer = self.api.create_streambuf();

    //let re = Regex::new(r"(x\s*=\s*(\d+)\s*,\s*y\s*=\s*(\d+))").unwrap();

    loop {
      let result = port.read_until(&buffer, "\n").await;
      match result {
        Err(err) => {
          if err.value == *OPERATION_ABORTED.get().unwrap() {
            return;
          }
          panic!("Unexpected error");
        },
        Ok(_) => {
          let line = buffer.to_string();
          buffer.consume(buffer.size());

          
          let parts = line.split(",");
          let numbers: Vec<f64> = parts.map(|t| t.parse::<f64>().unwrap()).collect();
          if numbers.len() == 2 {
            let mut samples = self.samples.borrow_mut();
            samples.clear();
            samples.push(numbers[0]);
            samples.push(numbers[1]);
          }
          self.api.ready();
        }
      }
    }
  }

  fn on_change(self: Rc<Self>, _source: &State, _action: i32, key: StateValue, value: StateValue) {
    println!("DemoNode::on_change {:?} {:?}", key, value);
    let StateValue::String(key_str) = key else {
      return
    };

    match key_str.as_str() {
      "Running" => {
        if StateValue::Bool(true) == value {
          let clone = self.clone();
          *self.task.borrow_mut() = Some(run_task(async move {
            clone.serial_loop().await;
          }));
        } else {
          *self.task.borrow_mut() = None
        }
      },
      "Port" => {
        let StateValue::String(val) = value else {
          return
        };
        *self.port.borrow_mut() = val;
      },
      _ => {}
    }
  }
}

struct SerialNode {
  inner: Rc<SerialNodeInner>
}

impl AnalogNode for SerialNode {
  fn data(
          &self,
          _channel: i32,
      ) -> impl Deref<Target = [f64]> {
    SliceDeref::new(self.inner.samples.borrow())
  }

  fn num_channels(&self) -> i32 { 1 }
  fn sample_interval(&self, _channel: i32) -> Duration {
    Duration::from_millis(1)
  }
  fn name(
          &self,
          _channel: i32,
      ) -> &str {
        "samples"
      }
}

impl Node for SerialNode {
  fn time(&self) -> Duration {
    *self.inner.time.borrow()
  }

  fn new(api: ThalamusAPI, state: State) -> Self {
    let inner = Rc::new(SerialNodeInner {
      state: state.clone(),
      state_connection: RefCell::new(None),
      api,
      samples: RefCell::new(Vec::<f64>::new()),
      time: RefCell::new(Duration::from_millis(0)),
      port: RefCell::new("".to_string()),
      task: RefCell::new(None)
    });

    let change_ref = Rc::downgrade(&inner);
    let callback = move |source: &State, action: i32, key: StateValue, value: StateValue| {
      change_ref.upgrade().map(|val| {
        val.on_change(source, action, key, value);
      });
    };
    {
      let mut temp = inner.state_connection.borrow_mut();
      *temp = Some(inner.state.connect(callback));
    }
    state.recap();

    SerialNode { inner }
  }
}

export_nodes!(
  ("EXT_DEMO", DemoNode),
  ("EXT_SERIAL", SerialNode)
);
