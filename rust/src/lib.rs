use std::ops::Deref;
use std::rc::{Rc, Weak};
use std::sync::mpsc::{self, Receiver, Sender};
use std::thread::{self, JoinHandle, sleep};
use std::{cell::RefCell, ptr};
use std::time::{Duration};
use serde::{Serialize, Deserialize};

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
use futures::select;
//use regex::Regex;

use crate::api::{ExtNode, Json, Sleeper, SleeperWaker, SliceDeref, StateKey, StrDeref, TaskScope, ThalamusAPI, run_task};

enum Message {
  Running(bool),
  Frequency(f64),
  Amplitude(f64),
  Samples(Vec<f64>),
  Time(Duration),
  Pulse(f64)
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
    SliceDeref::new(self.inner.samples.borrow(), None, None)
  }

  fn num_channels(&self) -> i32 { 1 }
  fn sample_interval(&self, _channel: i32) -> Duration {
    Duration::from_millis(1)
  }
  fn name(
          &self,
          _channel: i32,
      ) -> impl Deref<Target = str> {
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
  let mut pulse_time = Duration::from_secs(0);
  let mut pulse_val = 0.0;

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
      Ok(Message::Pulse(val)) => { 
        pulse_time = api.time();
        pulse_val = val;
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
      if last > pulse_time && last - pulse_time < Duration::from_millis(100) {
        samples.push(pulse_val)
      } else {
        let elapsed_s = elapsed.as_secs_f64();
        samples.push(amplitude*f64::sin(2.0*3.14*frequency*elapsed_s));
      }
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

#[derive(Serialize, Deserialize, Debug)]
struct PulseRequest {
  amplitude: f64
}

impl Node for DemoNode {
  fn time(&self) -> Duration {
    *self.inner.time.borrow()
  }
  
  fn process(&self, handle: api::Request, request: api::Json) {
    let text = request.to_string();
    let request: PulseRequest = serde_json::from_str(&text).unwrap();
    if let Some(out) = &*self.inner.output.borrow() {
      out.send(Message::Pulse(request.amplitude)).expect("Send Amplitude failed");
    }
    handle.respond(&Json::from_string(self.inner.api, "{\"success\": true}"));
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
  async fn serial_loop(this_weak: Weak<Self>) {
    let (port, buffer, timer) = {
      let Some(this) = this_weak.upgrade() else {
        return;
      };

      let mut samples = this.samples.borrow_mut();
      samples.clear();
      samples.push(0.0);
      samples.push(0.0);

      let port = this.api.create_serial_port();
      port.open(&this.port.borrow()).map_err(|e| { panic!("SERIAL ERROR: {}", e.message) } );
      port.set_baud_rate(115200).map_err(|e| { panic!("SERIAL ERROR: {}", e.message) } );

      let buffer = this.api.create_streambuf();

      let timer = this.api.create_timer();
      (port, buffer, timer)
    };

    //let re = Regex::new(r"(x\s*=\s*(\d+)\s*,\s*y\s*=\s*(\d+))").unwrap();

    let mut sleep = timer.sleep(Duration::from_millis(16));
    let mut read = port.read_until(&buffer, "\n");

    loop {
      println!("Read");
      select! {
        _ = sleep => {
          let Some(this) = this_weak.upgrade() else {
            return;
          };
          this.api.ready();
          sleep = timer.sleep(Duration::from_millis(16));
        },
        result = read => {
          match result {
            Err(err) => {
              println!("Err {}", err.message);
              if err.aborted() {
                return;
              }
              panic!("SERIAL ERROR: {}", err.message);
            },
            Ok(count) => {
              println!("Ok {}", count);
              let line = buffer.to_string();
              buffer.consume(buffer.size());

              
              println!("{}", line);
              let parts = line.trim().split(",");
              let numbers: Vec<f64> = parts
                  .map(|t| t.parse::<f64>())
                  .filter(|p| p.is_ok())
                  .map(|p| p.unwrap())
                  .collect();
              println!("numbers {:?}", numbers);

              let Some(this) = this_weak.upgrade() else {
                return;
              };

              if numbers.len() == 2 {
                let mut samples = this.samples.borrow_mut();
                samples.clear();
                samples.push(numbers[0]);
                samples.push(numbers[1]);
              }
              {
                let samples = this.samples.borrow();
                println!("samples {:?}", samples);
              }
              this.api.ready();
            }
          }
          read = port.read_until(&buffer, "\n");
        },
      };
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
          let clone = Rc::downgrade(&self);
          *self.task.borrow_mut() = Some(run_task(async move {
            SerialNodeInner::serial_loop(clone).await;
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
    
      match _channel {
          0 => SliceDeref::new(self.inner.samples.borrow(), Some(0), Some(1)),
          1 => SliceDeref::new(self.inner.samples.borrow(), Some(1), Some(2)),
          _ => SliceDeref::new(self.inner.samples.borrow(), Some(0), Some(0))
      }
  }

  fn num_channels(&self) -> i32 { 2 }
  fn sample_interval(&self, _channel: i32) -> Duration {
    Duration::from_millis(16)
  }
  fn name(
          &self,
          _channel: i32,
      ) -> impl Deref<Target = str> {
      match _channel {
          0 => "ch1",
          1 => "ch2",
          _ => "err"
      }
  }
}

impl Node for SerialNode {
  fn time(&self) -> Duration {
    *self.inner.time.borrow()
  }
  
  fn process(&self, handle: api::Request, _: api::Json) {
    handle.respond(&Json::from_string(self.inner.api, "{}"));
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

impl Drop for SerialNode {
  fn drop(&mut self) {
    println!("SerialNode.drop");
    //self.inner.task.borrow_mut().take().map(|t| { drop(t) });
  }
}

struct AlgebraNodeInner {
  state: State,
  state_connection: RefCell<Option<OnDrop>>,
  api: ThalamusAPI,
  samples: RefCell<Vec<f64>>,
  time: RefCell<Duration>,
  get_node: RefCell<OnDrop>,
  node_ready: RefCell<OnDrop>,
  scale: RefCell<f64>,
  sample_interval: RefCell<Duration>,
  channel_name: RefCell<String>
}

struct AlgebraNode {
  inner: Rc<AlgebraNodeInner>
}

impl AlgebraNodeInner {
  fn on_data(&self, node: &ExtNode) {
    let Some(analog) = node.analog() else {
      return;
    };

    if !analog.has_analog_data() {
      return;
    }

    if analog.num_channels() < 1 {
      return;
    }
    
    {
      let scale = *self.scale.borrow();
      let mut samples = self.samples.borrow_mut();
      samples.clear();
      for ele in analog.data(0) {
        samples.push(scale*ele);
      }

      let mut current_sample_interval = self.sample_interval.borrow_mut();
      let new_sample_interval = analog.sample_interval(0);
      if *current_sample_interval != new_sample_interval {
        *current_sample_interval = new_sample_interval;
        self.api.channels_changed();
      }

      let mut current_channel_name = self.channel_name.borrow_mut();
      let new_channel_name = analog.name(0);
      if *current_channel_name != new_channel_name {
        *current_channel_name = new_channel_name.to_string();
        self.api.channels_changed();
      }
    }

    self.api.ready();
  }

  fn on_change(self: Rc<Self>, _source: &State, _action: i32, key: StateValue, value: StateValue) {
    println!("DemoNode::on_change {:?} {:?}", key, value);
    let StateValue::String(key_str) = key else {
      return
    };

    match key_str.as_str() {
      "Node" => {
        let StateValue::String(val) = value else {
          return
        };
        let weak = Rc::downgrade(&self);
        *self.get_node.borrow_mut() = self.api.get_node(api::NodeSelector::Name(&val), move |node| {
          let weak2 = Weak::clone(&weak);
          weak.upgrade().map(|this| {
            *this.node_ready.borrow_mut() = node.subscribe(move |node| {

              weak2.upgrade().map(|this| {
                this.on_data(node);
              });

            });
          });
        });
      },
      "Scale" => {
        if let StateValue::Float(val) = value {
          *self.scale.borrow_mut() = val;
        }
      }
      _ => {}
    }
  }
}

impl AnalogNode for AlgebraNode {
  fn data(
          &self,
          _channel: i32,
      ) -> impl Deref<Target = [f64]> {
    
      match _channel {
          0 => SliceDeref::new(self.inner.samples.borrow(), None, None),
          _ => SliceDeref::new(self.inner.samples.borrow(), Some(0), Some(0))
      }
  }

  fn num_channels(&self) -> i32 { 1 }
  fn sample_interval(&self, _channel: i32) -> Duration {
    *self.inner.sample_interval.borrow()
  }
  fn name(
          &self,
          _channel: i32,
      ) -> impl Deref<Target = str> {
      match _channel {
          0 => StrDeref{ inner: self.inner.channel_name.borrow() },
          _ => panic!("Error")
      }
  }
}

impl Node for AlgebraNode {
  fn time(&self) -> Duration {
    *self.inner.time.borrow()
  }
  
  fn process(&self, _: api::Request, _: api::Json) {
    panic!("Unimplemented")
  }

  fn new(api: ThalamusAPI, state: State) -> Self {
    let inner = Rc::new(AlgebraNodeInner {
      state: state.clone(),
      state_connection: RefCell::new(None),
      api,
      samples: RefCell::new(Vec::<f64>::new()),
      time: RefCell::new(Duration::from_millis(0)),
      get_node: RefCell::new(OnDrop::noop()),
      node_ready: RefCell::new(OnDrop::noop()),
      scale: RefCell::new(1.0),
      sample_interval: RefCell::new(Duration::from_secs(0)),
      channel_name: RefCell::new("".to_string()),
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

    AlgebraNode { inner }
  }
}

export_nodes!(
  ("EXT_DEMO", DemoNode),
  ("EXT_SERIAL", SerialNode),
  ("EXT_ALGEBRA", AlgebraNode)
);
