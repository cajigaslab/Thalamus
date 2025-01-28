#include <remote_node.hpp>
#include <util.hpp>
#include <grpcpp/create_channel.h>
#include <thalamus.grpc.pb.h>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/quat_access.hpp>
#include <thalamus/thread.hpp>
#include <modalities_util.hpp>

using namespace thalamus;

struct RemoteNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context& io_context;
  boost::asio::steady_timer timer;
  boost::signals2::scoped_connection state_connection;
  std::string address;
  std::string name;
  std::string remote_node_name;
  std::string remote_node_channels;
  std::shared_ptr<grpc::Channel> channel;
  std::unique_ptr<thalamus_grpc::Thalamus::Stub> stub;
  NodeGraph* node_graph;
  std::unique_ptr<grpc::ClientContext> context;
  std::atomic_bool running;
  std::unique_ptr<grpc::ClientReaderWriter<thalamus_grpc::RemoteNodeMessage, thalamus_grpc::RemoteNodeMessage>> stream;
  std::thread grpc_thread;
  std::chrono::steady_clock::time_point ping_start;
  std::chrono::nanoseconds ping;
  RemoteNode* outer;
  std::vector<double> data;
  std::vector<std::chrono::nanoseconds> sample_intervals;
  std::vector<std::span<const double>> spans;
  std::vector<std::string> names;
  std::mutex mutex;
  std::condition_variable condition;
  bool ready = true;
  bool has_analog_data = false;
  bool has_xsens_data = false;
  std::vector<Segment> segments;
  std::chrono::nanoseconds time;
  std::chrono::nanoseconds remote_time;
  std::chrono::milliseconds ping_interval = 200ms;
  double ping_ms = 0;
  long long probe_size;
  std::unique_ptr<grpc::CompletionQueue> queue;
  std::string pose_name;
  double bps = 0;

  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* node_graph, RemoteNode* outer)
    : state(state), io_context(io_context), timer(io_context), node_graph(node_graph), running(false), outer(outer) {

    names.push_back("Ping");
    names.push_back("Bytes Per Second");
    spans.emplace_back();
    spans.emplace_back();
    sample_intervals.push_back(0ns);
    sample_intervals.push_back(0ns);

    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [] {});
    {
      std::lock_guard<std::mutex> lock(mutex);
      running = false;
      condition.notify_all();
      if(queue) {
        queue->Shutdown();
      }
    }
    if(grpc_thread.joinable()) {
      grpc_thread.join();
    }
  }

  enum StreamState {
    ANALOG_CONNECT,
    XSENS_CONNECT,
    ANALOG_READ,
    XSENS_READ,
    PING_CONNECT,
    PING,
    PONG,
    ANALOG_FINISH,
    XSENS_FINISH,
    PING_FINISH,
    STIM_CONNECT,
    STIM_NODE_WRITE,
    STIM_READ
  };
  thalamus_grpc::StimRequest stim_request;
  std::list<thalamus_grpc::StimRequest> stim_queue;
  std::unique_ptr<::grpc::ClientAsyncReaderWriter< ::thalamus_grpc::StimRequest, ::thalamus_grpc::StimResponse>> stim_stream;
  bool stim_ready = false;
  std::list<std::promise<thalamus_grpc::StimResponse>> stim_promise_queue;

  std::future<thalamus_grpc::StimResponse> queue_stim(thalamus_grpc::StimRequest&& request) {
    stim_queue.emplace_back(std::move(request));
    stim_promise_queue.emplace_back();
    return stim_promise_queue.back().get_future();
  }

  void send_stim() {
    if(stim_ready && !stim_queue.empty()) {
      stim_request = std::move(stim_queue.front());
      stim_queue.pop_front();
      stim_stream->Write(stim_request, reinterpret_cast<void*>(STIM_NODE_WRITE));
      stim_ready = false;
    }
  }

  std::string to_string(grpc_connectivity_state x) {
    switch(x) {
      case GRPC_CHANNEL_IDLE:
        return "GRPC_CHANNEL_IDLE";
      case GRPC_CHANNEL_CONNECTING:
        return "GRPC_CHANNEL_CONNECTING";
      case GRPC_CHANNEL_READY:
        return "GRPC_CHANNEL_READY";
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        return "GRPC_CHANNEL_TRANSIENT_FAILURE";
      case GRPC_CHANNEL_SHUTDOWN:
        return "GRPC_CHANNEL_SHUTDOWN";
      default:
        THALAMUS_ASSERT(false, "Unexpected grpc_connectivity_state value");
        return "";
    }
  }



  void grpc_target(std::shared_ptr<grpc::Channel> channel, std::string node, std::chrono::milliseconds ping_interval, long long probe_size) {
    set_current_thread_name("Remote Node GRPC");

    while(running) {
      {
        std::unique_lock<std::mutex> lock(mutex);
        stim_ready = false;
        stim_queue.clear();
        stim_promise_queue.clear();
        queue = std::make_unique<grpc::CompletionQueue>();
      }

      std::this_thread::sleep_for(1s);
      grpc_connectivity_state channel_state;
      while (running && (channel_state = channel->GetState(true)) != GRPC_CHANNEL_READY) {
        THALAMUS_LOG(info) << "Channel state " << to_string(channel_state);
        auto connect_deadline = std::chrono::system_clock::now() + 1s;
        channel->WaitForStateChange(channel_state, connect_deadline);
      }

      THALAMUS_LOG(info) << "Settled channel state " << to_string(channel_state);
      if(!running || channel_state != GRPC_CHANNEL_READY) {
        THALAMUS_LOG(info) << "Exiting grpc loop.";
        break;
      }
      THALAMUS_LOG(info) << "Connected";
      std::unique_ptr<thalamus_grpc::Thalamus::Stub> stub = thalamus_grpc::Thalamus::NewStub(channel);

      std::string probe_payload(probe_size, 0);
      for(auto i = 0ll;i < probe_size;++i) {
        probe_payload[i] = rand() % 256;
      }
      sample_intervals[0] = ping_interval;
      sample_intervals[1] = ping_interval;

      grpc::ClientContext analog_context;
      grpc::ClientContext xsens_context;
      grpc::ClientContext ping_context;
      grpc::ClientContext stim_context;
      thalamus_grpc::NodeSelector selector;
      selector.set_name(node);

      thalamus_grpc::AnalogRequest analog_request;
      *analog_request.mutable_node() = selector;
      auto analog_stream = stub->Asyncanalog(&analog_context, analog_request, queue.get(), reinterpret_cast<void*>(ANALOG_CONNECT));
      auto xsens_stream = stub->Asyncxsens(&xsens_context, selector, queue.get(), reinterpret_cast<void*>(XSENS_CONNECT));
      //stim_stream = stub->Asyncstim(&stim_context, queue.get(), reinterpret_cast<void*>(STIM_CONNECT));
      auto ping_stream = stub->Asyncping(&ping_context, queue.get(), reinterpret_cast<void*>(PING_CONNECT));

      thalamus_grpc::AnalogResponse analog_response;
      thalamus_grpc::XsensResponse xsens_response;
      thalamus_grpc::Ping ping;
      thalamus_grpc::StimResponse stim_response;
      ping.set_id(0);
      ping.mutable_payload()->assign(probe_payload);

      thalamus_grpc::Pong pong;
      auto ping_ready = false;
      size_t bytes_transferred = 0;

      auto next_ping_chrono = std::chrono::system_clock::now();
      std::map<unsigned int, std::chrono::steady_clock::time_point> ping_times;

      while(running) {
        size_t tag;
        auto ok = false;
        auto status = queue->AsyncNext(reinterpret_cast<void**>(&tag), &ok, next_ping_chrono);
        if(status == grpc::CompletionQueue::SHUTDOWN) {
          THALAMUS_LOG(info) << "Stream Shutdown";
          break;
        } else if (status == grpc::CompletionQueue::TIMEOUT) {
          if(ping_ready) {
            ping_ready = false;
            ping.set_id(ping.id()+1);
            ping_times[ping.id()] = std::chrono::steady_clock::now();
            ping_stream->Write(ping, reinterpret_cast<void*>(PING));
            bytes_transferred += ping.ByteSizeLong();
          }

          {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&] { return ready || !running; });
            if (!running) {
              continue;
            }
            ready = false;
            auto now = std::chrono::steady_clock::now();
            auto seconds = double(ping_interval.count())/decltype(ping_interval)::period::den;
            bps = bytes_transferred/seconds;
            bytes_transferred = 0;

            boost::asio::post(io_context, [this, now] {
              std::lock_guard<std::mutex> lock(mutex);

              this->time = now.time_since_epoch();
              for(auto& span : spans) {
                span = std::span<const double>();
              }
              spans[1] = std::span<const double>(&bps, &bps + 1);

              has_analog_data = true;
              outer->ready(outer);
              has_analog_data = false;
              ready = true;
              condition.notify_all();
            });
          }

          next_ping_chrono += ping_interval;
          continue;
        } else if (!ok) {
          THALAMUS_LOG(info) << "Stream Closed";
          channel->WaitForStateChange(channel_state, std::chrono::system_clock::now() + 1s);
          break;
        }

        switch(tag) {
          case ANALOG_CONNECT:
            analog_stream->Read(&analog_response, reinterpret_cast<void*>(ANALOG_READ));
            break;
          case XSENS_CONNECT:
            xsens_stream->Read(&xsens_response, reinterpret_cast<void*>(XSENS_READ));
            break;
          case PING_CONNECT:
            ping_ready = true;
            ping_stream->Read(&pong, reinterpret_cast<void*>(PONG));
            break;
          case STIM_CONNECT:
            {
              std::lock_guard<std::mutex> lock(mutex);
              stim_ready = true;
              thalamus_grpc::StimRequest request;
              *request.mutable_node() = selector;
              queue_stim(std::move(request));
              send_stim();
              stim_stream->Read(&stim_response, reinterpret_cast<void*>(STIM_READ));
            }
            break;
          case STIM_NODE_WRITE:
            {
              std::lock_guard<std::mutex> lock(mutex);
              stim_ready = true;
              send_stim();
            }
            break;
          case STIM_READ:
            {
              std::lock_guard<std::mutex> lock(mutex);
              stim_promise_queue.front().set_value(std::move(stim_response));
              stim_promise_queue.pop_front();
              stim_stream->Read(&stim_response, reinterpret_cast<void*>(STIM_READ));
            }
            break;
          case PING:
            ping_ready = true;
            break;
          case PONG:
            {
              bytes_transferred += pong.ByteSizeLong();
              auto i = ping_times.find(pong.id());
              THALAMUS_ASSERT(i != ping_times.end());
              auto now = std::chrono::steady_clock::now();
              auto ping_time = now - i->second;
              ping_times.erase(i);

              std::unique_lock<std::mutex> lock(mutex);
              condition.wait(lock, [&] { return ready || !running; });
              if (!running) {
                continue;
              }
              ready = false;

              boost::asio::post(io_context, [this, now, ping_time] {
                std::lock_guard<std::mutex> lock(mutex);

                this->time = now.time_since_epoch();
                ping_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ping_time).count();
                for(auto& span : spans) {
                  span = std::span<const double>();
                }
                spans.front() = std::span<const double>(&ping_ms, &ping_ms + 1);

                has_analog_data = true;
                outer->ready(outer);
                has_analog_data = false;
                ready = true;
                condition.notify_all();
              });
              ping_stream->Read(&pong, reinterpret_cast<void*>(PONG));
              break;
            }
          case ANALOG_READ: 
            {
              bytes_transferred += analog_response.ByteSizeLong();
              std::unique_lock<std::mutex> lock(mutex);
              condition.wait(lock, [&] { return ready || !running; });
              if (!running) {
                continue;
              }
              ready = false;

              auto channels_changed = false;
              if(names.size() != static_cast<size_t>(analog_response.spans_size())+2) {
                channels_changed = true;
              }

              time = std::chrono::steady_clock::now().time_since_epoch();
              remote_time = std::chrono::nanoseconds(analog_response.time());
              data.assign(analog_response.data().begin(), analog_response.data().end());
              spans.clear();
              spans.emplace_back();
              spans.emplace_back();
              std::vector<std::string> new_names(names.begin(), names.begin()+2);
              for(auto& span : analog_response.spans()) {
                spans.emplace_back(data.begin() + span.begin(), data.begin() + span.end());
                new_names.emplace_back(span.name());
              }
              sample_intervals.resize(analog_response.sample_intervals_size()+2);
              std::transform(analog_response.sample_intervals().begin(), analog_response.sample_intervals().end(),
                             sample_intervals.begin()+2,
                             [] (const auto& s) { return std::chrono::nanoseconds(s); });
              boost::asio::post(io_context, [&,channels_changed,new_names=std::move(new_names)] {
                if(channels_changed) {
                  outer->channels_changed(outer);
                }
                std::lock_guard<std::mutex> lock(mutex);
                names = std::move(new_names);
                has_analog_data = true;
                outer->ready(outer);
                has_analog_data = false;
                ready = true;
                condition.notify_all();
              });
              analog_stream->Read(&analog_response, reinterpret_cast<void*>(ANALOG_READ));
              break;
            }
          case XSENS_READ: 
            {
              bytes_transferred += xsens_response.ByteSizeLong();
              std::unique_lock<std::mutex> lock(mutex);
              condition.wait(lock, [&] { return ready || !running; });
              if (!running) {
                continue;
              }
              ready = false;

              time = std::chrono::steady_clock::now().time_since_epoch();
              segments.clear();
              for(auto& s : xsens_response.segments()) {
                segments.emplace_back();
                auto& segment = segments.back();
                segment.frame = s.frame();
                segment.segment_id = s.id();
                segment.time = s.time();
                boost::qvm::X(segment.position) = s.x();
                boost::qvm::Y(segment.position) = s.y();
                boost::qvm::Z(segment.position) = s.z();
                boost::qvm::S(segment.rotation) = s.q0();
                boost::qvm::X(segment.rotation) = s.q1();
                boost::qvm::Y(segment.rotation) = s.q2();
                boost::qvm::Z(segment.rotation) = s.q3();
              }
              pose_name = xsens_response.pose_name();
              boost::asio::post(io_context, [&] {
                std::lock_guard<std::mutex> lock(mutex);
                has_xsens_data = true;
                outer->ready(outer);
                has_xsens_data = false;
                ready = true;
                condition.notify_all();
              });
              xsens_stream->Read(&xsens_response, reinterpret_cast<void*>(XSENS_READ));
              break;
            }
        }
      }
      analog_context.TryCancel();
      xsens_context.TryCancel();
      ping_context.TryCancel();
      stim_context.TryCancel();
      {
        std::unique_lock<std::mutex> lock(mutex);
        stim_ready = false;
        stim_queue.clear();
        for(auto& p : stim_promise_queue) {
          p.set_value(thalamus_grpc::StimResponse());
        }
        stim_promise_queue.clear();
      }

      queue->Shutdown();
      size_t tag;
      auto ok = false;
      while(queue->Next(reinterpret_cast<void**>(&tag), &ok)) {}
      stim_stream.reset();
      {
        std::unique_lock<std::mutex> lock(mutex);
        queue.reset();
      }
    }
    //auto ok_status = ::grpc::Status::OK;
    //THALAMUS_LOG(info) << "Finishing streams";
    //analog_stream->Finish(&ok_status, reinterpret_cast<void*>(ANALOG_FINISH));
    //xsens_stream->Finish(&ok_status, reinterpret_cast<void*>(XSENS_FINISH));
    //ping_stream->Finish(&ok_status, reinterpret_cast<void*>(PING_FINISH));
    //auto analog_finished = false;
    //auto xsens_finished = false;
    //auto ping_finished = false;
    //while(!analog_finished || !xsens_finished || !ping_finished) {
    //  size_t tag;
    //  auto ok = false;
    //  auto status = queue->Next(reinterpret_cast<void**>(&tag), &ok);
    //  if(status == grpc::CompletionQueue::SHUTDOWN) {
    //    THALAMUS_LOG(info) << "Stream Shutdown";
    //    break;
    //  } else if (!ok) {
    //    THALAMUS_LOG(info) << "Stream Closed";
    //    break;
    //  }
    //  switch(tag) {
    //    case ANALOG_FINISH:
    //      analog_finished = true;
    //      THALAMUS_LOG(info) << "Analog stream finished";
    //      break;
    //    case XSENS_FINISH:
    //      xsens_finished = true;
    //      THALAMUS_LOG(info) << "Xsens stream finished";
    //      break;
    //    case PING_FINISH:
    //      ping_finished = true;
    //      THALAMUS_LOG(info) << "Ping stream finished";
    //      break;
    //  }
    //}
    THALAMUS_LOG(info) << "Streams finished";


    boost::asio::post(io_context, [this] {
      ((*state)["Running"]).assign(false);
    });
  }

  void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Address") {
      address = std::get<std::string>(v);
      //channel = node_graph->get_channel(value);
    } else if (key_str == "Node") {
      remote_node_name = std::get<std::string>(v);
    } else if (key_str == "Probe Frequency") {
      ping_interval = std::chrono::milliseconds(std::max(size_t(1000/std::get<double>(v)), size_t(1)));
    } else if (key_str == "Probe Size") {
      probe_size = std::get<long long>(v);
    } else if (key_str == "Running") {
      auto new_running = std::get<bool>(v);
      if(new_running == running) {
        return;
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        running = new_running;
        condition.notify_all();
        if (queue) {
          queue->Shutdown();
        }
      }
      if (!running) {
        if(grpc_thread.joinable()) {
          grpc_thread.join();
        }
        return;
      }
      auto channel = node_graph->get_channel(address);
      auto target = [this,channel,
                     remote_node_name=this->remote_node_name,
                     ping_interval=this->ping_interval,
                     probe_size=this->probe_size] {
        grpc_target(channel,
                    remote_node_name, ping_interval, probe_size);
      };
      grpc_thread = std::thread(target);
    }
  }
};

RemoteNode::RemoteNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* node_graph) : impl(new Impl(state, io_context, node_graph, this)) {
}

RemoteNode::~RemoteNode() {}

std::span<const double> RemoteNode::data(int channel) const {
  return impl->spans.at(channel);
}
int RemoteNode::num_channels() const {
  return impl->spans.size();
}
std::chrono::nanoseconds RemoteNode::sample_interval(int channel) const {
  return impl->sample_intervals.at(channel);
}
std::string_view RemoteNode::name(int channel) const {
  return impl->names.at(channel);
}
std::chrono::nanoseconds RemoteNode::time() const {
  return impl->time;
}
std::chrono::nanoseconds RemoteNode::remote_time() const {
  return impl->remote_time;
}
void RemoteNode::inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
  THALAMUS_ASSERT(false, "RemoteNode::inject unimplemented.");
}

std::string RemoteNode::type_name() {
  return "REMOTE";
}

std::span<MotionCaptureNode::Segment const> RemoteNode::segments() const {
  return std::span<MotionCaptureNode::Segment const>(impl->segments.begin(), impl->segments.end());
}
const std::string_view RemoteNode::pose_name() const {
  return impl->pose_name;
}
void RemoteNode::inject(const std::span<Segment const>&) {
  THALAMUS_ASSERT(false, "RemoteNode::inject unimplemented.");
}
bool RemoteNode::has_motion_data() const {
  return impl->has_xsens_data;
}
bool RemoteNode::has_analog_data() const {
  return impl->has_analog_data;
}

std::span<const std::string> RemoteNode::get_recommended_channels() const {
  return std::span<const std::string>(impl->names.begin(), impl->names.end());
}

std::future<thalamus_grpc::StimResponse> RemoteNode::stim(thalamus_grpc::StimRequest&& request) {
  std::lock_guard<std::mutex> lock(impl->mutex);
  auto result = impl->queue_stim(std::move(request));
  impl->send_stim();
  return result;
}

size_t RemoteNode::modalities() const { return infer_modalities<RemoteNode>(); }
