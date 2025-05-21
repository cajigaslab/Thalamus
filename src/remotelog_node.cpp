#include <modalities_util.hpp>
#include <remotelog_node.hpp>
#include <thalamus/thread.hpp>
#include <util.hpp>
#include <grpc_impl.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/qvm/quat_access.hpp>
#include <boost/qvm/vec_access.hpp>
#include <grpcpp/create_channel.h>
#include <thalamus.grpc.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace thalamus;

struct RemoteLogNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context &io_context;
  boost::asio::steady_timer timer;
  boost::signals2::scoped_connection state_connection;
  std::string address;
  std::string name;
  std::string remote_node_name;
  std::string remote_node_channels;
  NodeGraph *node_graph;
  std::unique_ptr<grpc::ClientContext> context;
  std::atomic_bool running;
  std::thread grpc_thread;
  std::chrono::steady_clock::time_point ping_start;
  RemoteLogNode *outer;
  std::vector<double> data;
  std::vector<std::chrono::nanoseconds> sample_intervals;
  std::vector<std::span<const double>> spans;
  std::vector<std::string> names;
  std::mutex mutex;
  std::condition_variable condition;
  bool ready = true;
  bool has_analog_data = false;
  bool has_xsens_data = false;
  std::chrono::nanoseconds time;
  std::chrono::nanoseconds remote_time;
  std::chrono::milliseconds configured_ping_interval = 200ms;
  double ping_ms = 0;
  long long configured_probe_size;
  std::unique_ptr<grpc::CompletionQueue> queue;
  std::string pose_name;
  double bps = 0;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_node_graph, RemoteLogNode *_outer)
      : state(_state), io_context(_io_context), timer(_io_context),
        node_graph(_node_graph), running(false), outer(_outer) {

    names.push_back("Ping");
    names.push_back("Bytes Per Second");
    spans.emplace_back();
    spans.emplace_back();
    sample_intervals.push_back(0ns);
    sample_intervals.push_back(0ns);

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [] {});
    {
      std::lock_guard<std::mutex> lock(mutex);
      running = false;
      condition.notify_all();
      if (queue) {
        queue->Shutdown();
      }
    }
    if (grpc_thread.joinable()) {
      grpc_thread.join();
    }
  }

  enum StreamState {
    LOG_CONNECT,
    LOG_READ,
    LOG_FINISH,
    PING_CONNECT,
    PING,
    PONG,
    PING_FINISH,
  };

  std::string to_string(grpc_connectivity_state x) {
    switch (x) {
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
    }
  }

  void grpc_target(std::shared_ptr<grpc::Channel> channel,
                   std::chrono::milliseconds ping_interval, size_t probe_size) {
    set_current_thread_name("Remote Log Node GRPC");

    while (running) {
      {
        std::unique_lock<std::mutex> lock(mutex);
        queue = std::make_unique<grpc::CompletionQueue>();
      }

      std::this_thread::sleep_for(1s);
      grpc_connectivity_state channel_state = GRPC_CHANNEL_IDLE;
      while (running &&
             (channel_state = channel->GetState(true)) != GRPC_CHANNEL_READY) {
        THALAMUS_LOG(info) << "Channel state " << to_string(channel_state);
        auto connect_deadline = std::chrono::system_clock::now() + 1s;
        channel->WaitForStateChange(channel_state, connect_deadline);
      }

      THALAMUS_LOG(info) << "Settled channel state "
                         << to_string(channel_state);
      if (!running || channel_state != GRPC_CHANNEL_READY) {
        THALAMUS_LOG(info) << "Exiting grpc loop.";
        break;
      }
      THALAMUS_LOG(info) << "Connected";
      std::unique_ptr<thalamus_grpc::Thalamus::Stub> stub =
          thalamus_grpc::Thalamus::NewStub(channel);

      std::string probe_payload(probe_size, 0);
      for (auto i = 0ull; i < probe_size; ++i) {
        probe_payload[i] = char(rand() % 256);
      }
      sample_intervals[0] = ping_interval;
      sample_intervals[1] = ping_interval;

      grpc::ClientContext log_context;
      grpc::ClientContext ping_context;

      thalamus_grpc::Empty log_request;
      auto log_stream =
          stub->Asynclogout(&log_context, log_request, queue.get(),
                            reinterpret_cast<void *>(LOG_CONNECT));
      auto ping_stream = stub->Asyncping(
          &ping_context, queue.get(), reinterpret_cast<void *>(PING_CONNECT));

      thalamus_grpc::Text log_response;
      thalamus_grpc::Ping ping;
      ping.set_id(0);
      ping.mutable_payload()->assign(probe_payload);

      thalamus_grpc::Pong pong;
      auto ping_ready = false;
      size_t bytes_transferred = 0;

      auto next_ping_chrono = std::chrono::system_clock::now();
      std::map<unsigned int, std::chrono::steady_clock::time_point> ping_times;

      while (running) {
        size_t tag;
        auto ok = false;
        auto status = queue->AsyncNext(reinterpret_cast<void **>(&tag), &ok,
                                       next_ping_chrono);
        if (status == grpc::CompletionQueue::SHUTDOWN) {
          THALAMUS_LOG(info) << "Stream Shutdown";
          break;
        } else if (status == grpc::CompletionQueue::TIMEOUT) {
          if (ping_ready) {
            ping_ready = false;
            ping.set_id(ping.id() + 1);
            ping_times[ping.id()] = std::chrono::steady_clock::now();
            ping_stream->Write(ping, reinterpret_cast<void *>(PING));
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
            auto seconds = double(ping_interval.count()) /
                           decltype(ping_interval)::period::den;
            bps = double(bytes_transferred) / seconds;
            bytes_transferred = 0;

            boost::asio::post(io_context, [this, now] {
              std::lock_guard<std::mutex> lock2(mutex);

              this->time = now.time_since_epoch();
              for (auto &span : spans) {
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
          channel->WaitForStateChange(channel_state,
                                      std::chrono::system_clock::now() + 1s);
          break;
        }

        switch (tag) {
        case LOG_CONNECT:
          log_stream->Read(&log_response,
                              reinterpret_cast<void *>(LOG_READ));
          break;
        case PING_CONNECT:
          ping_ready = true;
          ping_stream->Read(&pong, reinterpret_cast<void *>(PONG));
          break;
        case PING:
          ping_ready = true;
          break;
        case PONG: {
          bytes_transferred += pong.ByteSizeLong();
          auto i = ping_times.find(pong.id());
          THALAMUS_ASSERT(i != ping_times.end(), "Ping not found");
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
            std::lock_guard<std::mutex> lock2(mutex);

            this->time = now.time_since_epoch();
            ping_ms = double(
                std::chrono::duration_cast<std::chrono::milliseconds>(ping_time)
                    .count());
            for (auto &span : spans) {
              span = std::span<const double>();
            }
            spans.front() = std::span<const double>(&ping_ms, &ping_ms + 1);

            has_analog_data = true;
            outer->ready(outer);
            has_analog_data = false;
            ready = true;
            condition.notify_all();
          });
          ping_stream->Read(&pong, reinterpret_cast<void *>(PONG));
          break;
        }
        case LOG_READ: {
          bytes_transferred += log_response.ByteSizeLong();
          log_response.set_remote_time(log_response.time());
          log_response.set_time(uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
          
          boost::asio::post(io_context, [this,captured_log=std::move(log_response)] () {
            node_graph->get_service().log_signal(captured_log);
          });

          log_stream->Read(&log_response,
                              reinterpret_cast<void *>(LOG_READ));
          break;
        }
        }
      }
      log_context.TryCancel();
      queue->Shutdown();
      size_t tag;
      auto ok = false;
      while (queue->Next(reinterpret_cast<void **>(&tag), &ok)) {
      }
      {
        std::unique_lock<std::mutex> lock(mutex);
        queue.reset();
      }
    }
    THALAMUS_LOG(info) << "Log Stream finished";

    boost::asio::post(io_context,
                      [this] { ((*state)["Running"]).assign(false); });
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Address") {
      address = std::get<std::string>(v);
      // channel = node_graph->get_channel(value);
    } else if (key_str == "Probe Frequency") {
      configured_ping_interval = std::chrono::milliseconds(
          std::max(size_t(1000 / std::get<double>(v)), size_t(1)));
    } else if (key_str == "Probe Size") {
      configured_probe_size = std::get<long long>(v);
    } else if (key_str == "Running") {
      auto new_running = std::get<bool>(v);
      if (new_running == running) {
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
        if (grpc_thread.joinable()) {
          grpc_thread.join();
        }
        return;
      }
      auto new_channel = node_graph->get_channel(address);
      auto target = [this, new_channel,
                     c_ping_interval = this->configured_ping_interval,
                     c_probe_size = this->configured_probe_size] {
        grpc_target(new_channel, c_ping_interval,
                    size_t(c_probe_size));
      };
      grpc_thread = std::thread(target);
    }
  }
};

RemoteLogNode::RemoteLogNode(ObservableDictPtr state,
                       boost::asio::io_context &io_context,
                       NodeGraph *node_graph)
    : impl(new Impl(state, io_context, node_graph, this)) {}

RemoteLogNode::~RemoteLogNode() {}

std::span<const double> RemoteLogNode::data(int channel) const {
  return impl->spans.at(size_t(channel));
}
int RemoteLogNode::num_channels() const { return int(impl->spans.size()); }
std::chrono::nanoseconds RemoteLogNode::sample_interval(int channel) const {
  return impl->sample_intervals.at(size_t(channel));
}
std::string_view RemoteLogNode::name(int channel) const {
  return impl->names.at(size_t(channel));
}
std::chrono::nanoseconds RemoteLogNode::time() const { return impl->time; }
std::chrono::nanoseconds RemoteLogNode::remote_time() const {
  return impl->remote_time;
}
void RemoteLogNode::inject(const thalamus::vector<std::span<double const>> &,
                        const thalamus::vector<std::chrono::nanoseconds> &,
                        const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "RemoteLogNode::inject unimplemented.");
}

bool RemoteLogNode::has_analog_data() const { return impl->has_analog_data; }

std::string RemoteLogNode::type_name() { return "REMOTE_LOG"; }

size_t RemoteLogNode::modalities() const { return infer_modalities<RemoteLogNode>(); }
