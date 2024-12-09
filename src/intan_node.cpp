#include <intan_node.hpp>
#include <boost/asio.hpp>
#include <vector>
#include <map>
#include <functional>
#include <string>
#include <base_node.hpp>
#include <absl/strings/str_split.h>
#include <state.hpp>
#include <boost/signals2.hpp>
#include <modalities.h>
#include <numeric>
#include <boost/endian/conversion.hpp> 
#include <boost/exception/diagnostic_information.hpp>
#include <thalamus/async.hpp>

using namespace thalamus;
using namespace std::chrono_literals;
using namespace std::placeholders;

struct IntanNode::Impl {
  template<typename T>
  struct VarGuard {
    T& var;
    T end;
    VarGuard(T& var, T initial, T end) : var(var), end(end) {
      var = initial;
    }
    ~VarGuard() {
      var = end;
    }
  };

  struct Socket {
    boost::asio::io_context& io_context;
    boost::asio::ip::tcp::socket socket;
    CoCondition condition;
    std::stringstream stream;
    std::string name;
    bool reading = false;
    ObservableDictPtr state;
    Socket(boost::asio::io_context& io_context, const std::string& name, ObservableDictPtr state)
      : io_context(io_context), socket(io_context), condition(io_context), name(name), state(state) {}

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
        while(true) {
          auto count = co_await socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)));
          stream << std::string(buffer, count);
          condition.notify();
        }
      } catch(boost::system::system_error& e) {
        if(e.code() == boost::asio::error::shut_down) {
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

    template<typename DURATION>
    boost::asio::awaitable<bool> wait_for_read(DURATION duration) {
      auto result = co_await condition.wait(duration);
      co_return result != std::cv_status::timeout;
    }
  };

  ObservableDictPtr state;
  size_t observer_id;
  boost::signals2::scoped_connection state_connection;
  boost::asio::io_context& io_context;
  boost::asio::high_resolution_timer timer;
  Socket command_socket;
  boost::asio::ip::tcp::socket waveform_socket;
  size_t num_channels;
  size_t buffer_size;
  int frame = 0;
  int channel = 0;
  size_t num_samples = 0;
  unsigned int timestamp;
  std::chrono::nanoseconds time;
  std::chrono::nanoseconds sample_interval;
  std::vector<short> short_buffer;
  std::vector<double> double_buffer;
  ObservableListPtr channels;
  std::map<size_t, std::function<void(Node*)>> observers;
  std::vector<std::vector<double>> data;
  std::vector<std::string> names;
  //double sample_rate;
  size_t counter = 0;
  std::string address = "localhost";
  long long command_port = 5000;
  long long waveform_port = 5001;
  unsigned char command_buffer[1024];
  std::string sample_rate_response;
  unsigned char waveform_buffer[16384];
  IntanNode* outer;
  bool is_running = false;
  bool is_connected = false;
  bool getting_sample_rate = false;
public:
  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, IntanNode* outer)
    : state(state)
    , io_context(io_context)
    , timer(io_context)
    , command_socket(io_context, "command", state)
    , waveform_socket(io_context)
    , outer(outer)
    , connecting_condition(io_context) {
    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    (*state)["Running"].assign(false);
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false);
  }
  bool connecting = false;
  bool connected = false;
  bool streaming = false;
  CoCondition connecting_condition;
  bool got_magic_number = false;

  void reset_waveform_loop() {
    got_magic_number = false;
  }

  boost::asio::awaitable<void> waveform_loop() {
    try {
      int channel = -1;
      size_t offset = 0;
      size_t filled = 0;
      unsigned char buffer[16384];
      got_magic_number = false;
      while(true) {
        if(filled == sizeof(buffer)) {
          std::copy(buffer + offset, buffer + filled, buffer);
          filled -= offset;
          offset = 0;
        }
        auto count = co_await waveform_socket.async_receive(boost::asio::buffer(buffer+filled, sizeof(buffer)-filled));
        if(!streaming) {
          continue;
        }
        filled += count;
        if(!got_magic_number) {
          while(!got_magic_number && filled - offset >= 4) {
            auto pos = buffer + offset;
            unsigned int magic = pos[0] | pos[1] << 8 | pos[2] << 16 | pos[3] << 24;
            got_magic_number = magic == 0x2ef07a08;
            offset += got_magic_number ? 4 : 1;
            frame = 0;
          }
        }
        while(true) {
          if(channel == -1) {
            if(filled - offset >= 4) {
              auto pos = buffer + offset;
              unsigned int timestamp = pos[0] | pos[1] << 8 | pos[2] << 16 | pos[3] << 24;
              data[0].push_back(timestamp);
              offset += 4;
              ++channel;
            } else {
              break;
            }
          } else if(filled - offset >= 2) {
            auto pos = buffer + offset;
            unsigned short sample = pos[0] | pos[1] << 8;
            data[channel+1].push_back(sample);
            offset += 2;
            ++channel;
            if(channel == num_channels) {
              channel = -1;
              ++frame;
              if(frame == 128) {
                got_magic_number = false;
                break;
              }
            }
          }
        }

        num_samples = std::accumulate(data.begin(), data.end(), std::numeric_limits<size_t>::max(), [](size_t a, auto& b) { return std::min(a, b.size()); });
        if(num_samples > 0) { 
          outer->ready(outer);
          for(auto& d: data) {
            d.erase(d.begin(), d.begin() + num_samples);
          }
        }
      }
    } catch(std::exception& e) {
      THALAMUS_LOG(error) << boost::diagnostic_information(e);
      (*state)["Connected"].assign(false);
      disconnect();
    }
  }

  boost::asio::awaitable<void> do_connect() {
    if (connected) {
      co_return;
    }
    if(connecting) {
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
      auto endpoints = co_await resolver.async_resolve(address, std::to_string(command_port));
      co_await boost::asio::async_connect(command_socket.socket, endpoints);

      endpoints = co_await resolver.async_resolve(address, std::to_string(waveform_port));
      co_await boost::asio::async_connect(waveform_socket, endpoints);
      connected = true;
      (*state)["Connected"].assign(true);

      boost::asio::co_spawn(io_context, waveform_loop(), boost::asio::detached);
    } catch(std::exception& e) {
      THALAMUS_LOG(error) << boost::diagnostic_information(e);
      (*state)["Connected"].assign(false);
      disconnect();
      co_return;
    }
  }

  void disconnect() {
    boost::system::error_code ec;
    ec = command_socket.socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    if(ec) {
      THALAMUS_LOG(error) << ec.what();
    }
    ec = command_socket.socket.close(ec);
    if(ec) {
      THALAMUS_LOG(error) << ec.what();
    }

    ec = waveform_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    if(ec) {
      THALAMUS_LOG(error) << ec.what();
    }
    ec = waveform_socket.close(ec);
    if(ec) {
      THALAMUS_LOG(error) << ec.what();
    }
    connected = false;
  }

  boost::asio::awaitable<void> start_stream() {
    try {
      co_await do_connect();
      if(!connected) {
        co_return;
      }

      if(streaming) {
        co_return;
      }
      streaming = true;
      reset_waveform_loop();

      std::string command = "execute clearalldataoutputs;\n";
      co_await boost::asio::async_write(command_socket.socket, boost::asio::const_buffer(command.data(), command.size()));

      if(channels) {
        names.assign(1, "timestamp");
        for(auto i = channels->begin();i != channels->end();++i) {
          std::string text = *i;
          names.push_back(text);
          command = absl::StrFormat("set %s.tcpdataoutputenabled true;\n", text);
          co_await boost::asio::async_write(command_socket.socket, boost::asio::const_buffer(command.data(), command.size()));
        }
        num_channels = channels->size();
      } else {
        num_channels = 0;
      }
      data.assign(num_channels+1, std::vector<double>());

      command = "get sampleratehertz;\n";
      co_await boost::asio::async_write(command_socket.socket, boost::asio::const_buffer(command.data(), command.size()));
      command_socket.start_reading();

      auto collecting = true;
      while(collecting) {
        collecting = co_await command_socket.wait_for_read(100ms);
      }

      auto sample_rate_response = command_socket.take();
      std::vector<std::string> tokens = absl::StrSplit(sample_rate_response, ' ');
      sample_rate_response = "";
      if(tokens.size() < 3 || *(tokens.end()-3) != "Return:" || *(tokens.end()-2) != "SampleRateHertz") {
        THALAMUS_LOG(error) << "Unexpected response to get sampleratehertz: " << sample_rate_response;
        (*state)["Running"].assign(false);
        co_return;
      }

      std::string digits = "";
      for(auto c : *(tokens.end()-1)) {
        if(std::isdigit(c)) {
          digits.push_back(c);
        } else {
          break;
        }
      }
      int samplerate;
      auto success = absl::SimpleAtoi(digits, &samplerate);
      if(!success) {
        THALAMUS_LOG(error) << "Failed to parse sample rate: " << *(tokens.end()-1);
        (*state)["Running"].assign(false);
        co_return;
      }
      sample_interval = std::chrono::nanoseconds(std::nano::den/samplerate);
      outer->channels_changed(outer);

      THALAMUS_LOG(info) << "Starting " << sample_interval.count();
      command = "set runmode record;\n";
      co_await boost::asio::async_write(command_socket.socket, boost::asio::const_buffer(command.data(), command.size()));

    } catch(boost::system::system_error& e) {
      THALAMUS_LOG(error) << boost::diagnostic_information(e);
      (*state)["Running"].assign(false);
      co_return;
    }
  }

  boost::asio::awaitable<void> stop_stream() {
      if(!streaming) {
        co_return;
      }
      streaming = false;

      THALAMUS_LOG(info) << "Stopping " << sample_interval.count();
      std::string command = "set runmode stop;\n";
      co_await boost::asio::async_write(command_socket.socket, boost::asio::const_buffer(command.data(), command.size()));
  }

  void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
    auto key_str = std::get<std::string>(k);
    if(key_str == "Address") {
      address = std::get<std::string>(v);
    } else if (key_str == "Command Port") {
      command_port = std::get<long long>(v);
    } else if (key_str == "Waveform Port") {
      waveform_port = std::get<long long>(v);
    } else if (key_str == "Connected") {
      auto is_connected = std::get<bool>(v);
      if(is_connected) {
        boost::asio::co_spawn(io_context, do_connect(), boost::asio::detached);
      } else {
        disconnect();
      }
    } else if (key_str == "Running") {
      auto is_running = std::get<bool>(v);
      if (is_running) {
        boost::asio::co_spawn(io_context, start_stream(), boost::asio::detached);
      } else {
        boost::asio::co_spawn(io_context, stop_stream(), boost::asio::detached);
      }
    } else if (key_str == "Channels") {
      channels = std::get<ObservableListPtr>(v);
    }
  }
};

IntanNode::IntanNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
  : impl(new Impl(state, io_context, graph, this)) {}

IntanNode::~IntanNode() {}
  
std::span<const double> IntanNode::data(int channel) const {
  auto& data = impl->data[channel];
  return std::span<const double>(data.begin(), data.begin()+impl->num_samples);
}

std::string_view IntanNode::name(int channel) const {
  return impl->names[channel];
}

int IntanNode::num_channels() const {
  return impl->data.size();
}

std::chrono::nanoseconds IntanNode::sample_interval(int i) const {
  return impl->sample_interval;
}

std::chrono::nanoseconds IntanNode::time() const {
  return impl->time;
}

std::string IntanNode::type_name() {
  return "INTAN";
}

void IntanNode::inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) {}

size_t IntanNode::modalities() const {
  return THALAMUS_MODALITY_ANALOG;
}
