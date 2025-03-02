#include <thalamus/tracing.hpp>
#include <base_node.hpp>
#include <functional>
#include <intan_node.hpp>
#include <map>
#include <modalities.h>
#include <numeric>
#include <state.hpp>
#include <string>
#include <vector>
#include <thalamus/async.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <absl/strings/str_split.h>
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/signals2.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace thalamus;
using namespace std::chrono_literals;
using namespace std::placeholders;

struct IntanNode::Impl {
  template <typename T> struct VarGuard {
    T &var;
    T end;
    VarGuard(T &_var, T initial, T _end) : var(_var), end(_end) {
      var = initial;
    }
    ~VarGuard() { var = end; }
  };

  struct Socket {
    boost::asio::io_context &io_context;
    boost::asio::ip::tcp::socket socket;
    CoCondition condition;
    std::stringstream stream;
    std::string name;
    bool reading = false;
    ObservableDictPtr state;
    Socket(boost::asio::io_context &_io_context, const std::string &_name,
           ObservableDictPtr _state)
        : io_context(_io_context), socket(_io_context), condition(_io_context),
          name(_name), state(_state) {}

    std::string take() {
      auto result = stream.str();
      stream.str("");
      return result;
    }

    boost::asio::awaitable<void> read_loop() {
      reading = true;
      Finally f([&] { reading = false; });
      try {
        char buffer[1024];
        while (true) {
          auto count = co_await socket.async_receive(
              boost::asio::buffer(buffer, sizeof(buffer)));
          stream << std::string(buffer, count);
          condition.notify();
        }
      } catch (boost::system::system_error &e) {
        if (e.code() == boost::asio::error::shut_down) {
          THALAMUS_LOG(info) << name << " socket shutdown";
        } else {
          THALAMUS_LOG(error) << boost::diagnostic_information(e);
        }
        (*state)["Running"].assign(false);
      }
      co_return;
    }

    void start_reading() {
      boost::asio::co_spawn(io_context, read_loop(), boost::asio::detached);
    }

    template <typename DURATION>
    boost::asio::awaitable<bool> wait_for_read(DURATION duration) {
      auto result = co_await condition.wait(duration);
      co_return result != std::cv_status::timeout;
    }
  };

  ObservableDictPtr state;
  size_t observer_id;
  boost::signals2::scoped_connection state_connection;
  boost::asio::io_context &io_context;
  Socket command_socket;
  boost::asio::ip::tcp::socket waveform_socket;
  size_t num_channels;
  size_t buffer_size;
  size_t num_samples = 0;
  std::chrono::nanoseconds time;
  std::chrono::nanoseconds sample_interval;
  std::vector<short> short_buffer;
  std::vector<double> double_buffer;
  ObservableListPtr channels;
  std::map<size_t, std::function<void(Node *)>> observers;
  std::vector<std::vector<double>> data;
  std::vector<std::string> names;
  // double sample_rate;
  size_t counter = 0;
  std::string address = "localhost";
  long long command_port = 5000;
  long long waveform_port = 5001;
  unsigned char command_buffer[1024];
  unsigned char waveform_buffer[16384];
  IntanNode *outer;
  bool is_running = false;
  bool is_connected = false;
  bool getting_sample_rate = false;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *, IntanNode *_outer)
      : state(_state), io_context(_io_context),
        command_socket(io_context, "command", state),
        waveform_socket(io_context), outer(_outer),
        connecting_condition(io_context) {
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    (*state)["Running"].assign(false);
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() { (*state)["Running"].assign(false); }
  bool connecting = false;
  bool connected = false;
  bool streaming = false;
  CoCondition connecting_condition;

  boost::asio::awaitable<void> waveform_loop() {
    try {
      size_t offset = 0;
      size_t filled = 0;
      unsigned char buffer[16384];
      boost::asio::steady_timer timer(io_context);

      auto receive = [&]() -> boost::asio::awaitable<void> {
        if (filled == sizeof(buffer)) {
          std::copy(buffer + offset, buffer + filled, buffer);
          filled -= offset;
          offset = 0;
        }
        auto count = co_await waveform_socket.async_receive(
          boost::asio::buffer(buffer + filled, sizeof(buffer) - filled));
        filled += count;
      };
      while (true) {
        if (!streaming) {
          timer.expires_after(1s);
          co_await timer.async_wait();
          continue;
        }

        auto got_magic_number = false;
        TRACE_EVENT_BEGIN("intan", "Parse Magic Number");
        while (!got_magic_number) {
          while (filled - offset < 4) {
            TRACE_EVENT_END("intan");
            co_await receive();
            TRACE_EVENT_BEGIN("intan", "Parse Magic Number");
          }
          auto pos = buffer + offset;
          unsigned int magic = uint32_t(pos[0]) | uint32_t(pos[1]) << 8 |
            uint32_t(pos[2]) << 16 |
            uint32_t(pos[3]) << 24;
          got_magic_number = magic == 0x2ef07a08;
          offset += got_magic_number ? 4 : 1;
        }
        TRACE_EVENT_END("intan");

        int frame = 0;
        TRACE_EVENT_BEGIN("intan", "Parse Frames");
        while (frame < 128) {
          while (filled - offset < 4) {
            TRACE_EVENT_END("intan");
            co_await receive();
            TRACE_EVENT_BEGIN("intan", "Parse Frames");
          }
          auto pos = buffer + offset;
          unsigned int timestamp =
            uint32_t(pos[0]) | uint32_t(pos[1]) << 8 |
            uint32_t(pos[2]) << 16 | uint32_t(pos[3]) << 24;
          data[0].push_back(timestamp);
          offset += 4;

          size_t channel = 0;
          while (channel < num_channels) {
            while (filled - offset < 2) {
              TRACE_EVENT_END("intan");
              co_await receive();
              TRACE_EVENT_BEGIN("intan", "Parse Frames");
            }
            pos = buffer + offset;
            auto sample = uint16_t(pos[0] | pos[1] << 8);
            data[size_t(channel + 1)].push_back(sample);
            offset += 2;
            ++channel;
          }
          ++frame;
        }
        TRACE_EVENT_END("intan");

        TRACE_EVENT("intan", "ready");
        num_samples = 128;
        outer->ready(outer);
        for (auto &d : data) {
          d.clear();
        }
      }
    } catch (std::exception &e) {
      THALAMUS_LOG(error) << boost::diagnostic_information(e);
      (*state)["Connected"].assign(false);
      disconnect();
    }
  }

  boost::asio::awaitable<void> do_connect() {
    if (connected) {
      co_return;
    }
    if (connecting) {
      co_await connecting_condition.wait();
      co_return;
    }
    connecting = true;
    connected = false;
    Finally f([&] {
      connecting = false;
      connecting_condition.notify();
    });
    try {
      boost::asio::ip::tcp::resolver resolver(io_context);
      auto endpoints = co_await resolver.async_resolve(
          address, std::to_string(command_port));
      co_await boost::asio::async_connect(command_socket.socket, endpoints);

      endpoints = co_await resolver.async_resolve(
          address, std::to_string(waveform_port));
      co_await boost::asio::async_connect(waveform_socket, endpoints);
      connected = true;
      (*state)["Connected"].assign(true);

      boost::asio::co_spawn(io_context, waveform_loop(), boost::asio::detached);
    } catch (std::exception &e) {
      THALAMUS_LOG(error) << boost::diagnostic_information(e);
      (*state)["Connected"].assign(false);
      disconnect();
      co_return;
    }
  }

  void disconnect() {
    boost::system::error_code ec;
    ec = command_socket.socket.shutdown(
        boost::asio::ip::tcp::socket::shutdown_both, ec);
    if (ec) {
      THALAMUS_LOG(error) << ec.what();
    }
    ec = command_socket.socket.close(ec);
    if (ec) {
      THALAMUS_LOG(error) << ec.what();
    }

    ec = waveform_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                                  ec);
    if (ec) {
      THALAMUS_LOG(error) << ec.what();
    }
    ec = waveform_socket.close(ec);
    if (ec) {
      THALAMUS_LOG(error) << ec.what();
    }
    connected = false;
  }

  boost::asio::awaitable<void> start_stream() {
    try {
      co_await do_connect();
      if (!connected) {
        co_return;
      }

      if (streaming) {
        co_return;
      }
      streaming = true;

      std::string command = "execute clearalldataoutputs;\n";
      co_await boost::asio::async_write(
          command_socket.socket,
          boost::asio::const_buffer(command.data(), command.size()));

      if (channels) {
        names.assign(1, "timestamp");
        for (auto i = channels->begin(); i != channels->end(); ++i) {
          std::string text = *i;
          names.push_back(text);
          command =
              absl::StrFormat("set %s.tcpdataoutputenabled true;\n", text);
          co_await boost::asio::async_write(
              command_socket.socket,
              boost::asio::const_buffer(command.data(), command.size()));
        }
        num_channels = channels->size();
      } else {
        num_channels = 0;
      }
      data.assign(num_channels + 1, std::vector<double>());

      command = "get sampleratehertz;\n";
      co_await boost::asio::async_write(
          command_socket.socket,
          boost::asio::const_buffer(command.data(), command.size()));
      command_socket.start_reading();

      co_await command_socket.wait_for_read(10s);
      auto collecting = true;
      while (collecting) {
        collecting = co_await command_socket.wait_for_read(100ms);
      }

      auto sample_rate_response = command_socket.take();
      THALAMUS_LOG(info) << "get sampleratehertz response:" << sample_rate_response;
      std::vector<std::string> tokens =
          absl::StrSplit(sample_rate_response, ' ');
      sample_rate_response = "";
      if (tokens.size() < 3 || !absl::EndsWith(*(tokens.end() - 3), "Return:") ||
          *(tokens.end() - 2) != "SampleRateHertz") {
        THALAMUS_LOG(error) << "Unexpected response to get sampleratehertz: "
                            << sample_rate_response;
        (*state)["Running"].assign(false);
        co_return;
      }

      std::string digits = "";
      for (auto c : *(tokens.end() - 1)) {
        if (std::isdigit(c)) {
          digits.push_back(c);
        } else {
          break;
        }
      }
      int samplerate;
      auto success = absl::SimpleAtoi(digits, &samplerate);
      if (!success) {
        THALAMUS_LOG(error)
            << "Failed to parse sample rate: " << *(tokens.end() - 1);
        (*state)["Running"].assign(false);
        co_return;
      }
      sample_interval = std::chrono::nanoseconds(std::nano::den / samplerate);
      outer->channels_changed(outer);

      THALAMUS_LOG(info) << "Starting " << sample_interval.count();
      command = "set runmode record;\n";
      co_await boost::asio::async_write(
          command_socket.socket,
          boost::asio::const_buffer(command.data(), command.size()));

    } catch (boost::system::system_error &e) {
      THALAMUS_LOG(error) << boost::diagnostic_information(e);
      (*state)["Running"].assign(false);
      co_return;
    }
  }

  boost::asio::awaitable<void> stop_stream() {
    if (!streaming) {
      co_return;
    }
    streaming = false;

    THALAMUS_LOG(info) << "Stopping " << sample_interval.count();
    std::string command = "set runmode stop;\n";
    co_await boost::asio::async_write(
        command_socket.socket,
        boost::asio::const_buffer(command.data(), command.size()));
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Address") {
      address = std::get<std::string>(v);
    } else if (key_str == "Command Port") {
      command_port = std::get<long long>(v);
    } else if (key_str == "Waveform Port") {
      waveform_port = std::get<long long>(v);
    } else if (key_str == "Connected") {
      auto new_is_connected = std::get<bool>(v);
      if (new_is_connected) {
        boost::asio::co_spawn(io_context, do_connect(), boost::asio::detached);
      } else {
        disconnect();
      }
    } else if (key_str == "Running") {
      auto new_is_running = std::get<bool>(v);
      if (new_is_running) {
        boost::asio::co_spawn(io_context, start_stream(),
                              boost::asio::detached);
      } else {
        boost::asio::co_spawn(io_context, stop_stream(), boost::asio::detached);
      }
    } else if (key_str == "Channels") {
      channels = std::get<ObservableListPtr>(v);
    }
  }
};

IntanNode::IntanNode(ObservableDictPtr state,
                     boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

IntanNode::~IntanNode() {}

std::span<const double> IntanNode::data(int channel) const {
  auto &data = impl->data[size_t(channel)];
  return std::span<const double>(data.begin(),
                                 data.begin() + int64_t(impl->num_samples));
}

std::string_view IntanNode::name(int channel) const {
  return impl->names[size_t(channel)];
}

int IntanNode::num_channels() const { return int(impl->data.size()); }

std::chrono::nanoseconds IntanNode::sample_interval(int) const {
  return impl->sample_interval;
}

std::chrono::nanoseconds IntanNode::time() const { return impl->time; }

std::string IntanNode::type_name() { return "INTAN"; }

void IntanNode::inject(const thalamus::vector<std::span<double const>> &,
                       const thalamus::vector<std::chrono::nanoseconds> &,
                       const thalamus::vector<std::string_view> &) {}

size_t IntanNode::modalities() const { return THALAMUS_MODALITY_ANALOG; }
