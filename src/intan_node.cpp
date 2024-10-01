#include <intan_node.h>
#include <boost/asio.hpp>
#include <vector>
#include <map>
#include <functional>
#include <string>
#include <iostream>
#include <base_node.h>
#include <absl/strings/str_split.h>
#include <state.hpp>
#include <boost/signals2.hpp>
#include <modalities.h>
#include <numeric>
#include <boost/endian/conversion.hpp> 

using namespace thalamus;
using namespace std::chrono_literals;
using namespace std::placeholders;

struct IntanNode::Impl {
  ObservableDictPtr state;
  size_t observer_id;
  boost::signals2::scoped_connection state_connection;
  boost::asio::io_context& io_context;
  boost::asio::high_resolution_timer timer;
  boost::asio::ip::tcp::socket command_socket;
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
    , command_socket(io_context)
    , waveform_socket(io_context)
    , outer(outer) {
    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    (*state)["Running"].assign(false);
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false);
  }

  void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
    auto key_str = std::get<std::string>(k);
    if(key_str == "Address") {
      address = std::get<std::string>(v);
    } else if (key_str == "Command Port") {
      command_port = std::get<long long>(v);
    } else if (key_str == "Waveform Port") {
      waveform_port = std::get<long long>(v);
    } else if (key_str == "Running") {
      is_running = std::get<bool>(v);
      if (is_running) {
        boost::asio::ip::tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(address, std::to_string(command_port));
        boost::system::error_code ec;
        boost::asio::connect(command_socket, endpoints, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
          (*state)["Running"].assign(false);
          return;
        }
        is_connected = true;

        endpoints = resolver.resolve(address, std::to_string(waveform_port));
        boost::asio::connect(waveform_socket, endpoints, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
          (*state)["Running"].assign(false);
          return;
        }
        wave_parser_state = WaveParserState::MAGIC_NUMBER;
        waveform_socket.async_receive(boost::asio::buffer(waveform_buffer, sizeof(waveform_buffer)), std::bind(&Impl::on_receive_waveform, this, _1, _2));

        std::string command = "execute clearalldataoutputs;";
        command_socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec);

        if(channels) {
          names.assign(1, "timestamp");
          for(auto i = channels->begin();i != channels->end();++i) {
            std::string text = *i;
            names.push_back(text);
            auto command = absl::StrFormat("set %s.tcpdataoutputenabled true;", text);
            command_socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec);
            if(ec) {
              THALAMUS_LOG(error) << ec.what();
              (*state)["Running"].assign(false);
              return;
            }
          }
          num_channels = channels->size();
        } else {
          num_channels = 0;
        }
        data.assign(num_channels+1, std::vector<double>());

        command = "get sampleratehertz;";
        command_socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
          (*state)["Running"].assign(false);
          return;
        }
        getting_sample_rate = true;
        sample_rate_response = "";
        timer.expires_after(1s);
        timer.async_wait([&](const boost::system::error_code& ec) {
          if(ec) {
            return;
          }

          getting_sample_rate = false;
          std::vector<std::string> tokens = absl::StrSplit(sample_rate_response, ' ');
          sample_rate_response = "";
          if(tokens.size() < 3 || tokens[0] != "Return:" || tokens[1] != "SampleRateHertz") {
            THALAMUS_LOG(error) << "Unexpected response to get sampleratehertz: " << sample_rate_response;
            (*state)["Running"].assign(false);
            return;
          }

          std::string digits = "";
          for(auto c : tokens[2]) {
            if(std::isdigit(c)) {
              digits.push_back(c);
            } else {
              break;
            }
          }
          int samplerate;
          auto success = absl::SimpleAtoi(digits, &samplerate);
          if(!success) {
            THALAMUS_LOG(error) << "Failed to parse sample rate: " << tokens[2];
            (*state)["Running"].assign(false);
            return;
          }
          sample_interval = std::chrono::nanoseconds(std::nano::den/samplerate);
          outer->channels_changed(outer);

          THALAMUS_LOG(info) << "Starting " << sample_interval.count();
          std::string command = "set runmode run;";
          boost::system::error_code ec2;
          command_socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec2);
          if(ec) {
            THALAMUS_LOG(error) << ec.what();
            (*state)["Running"].assign(false);
            return;
          }
        });

        command_socket.async_receive(boost::asio::buffer(command_buffer, sizeof(command_buffer)), std::bind(&Impl::on_receive_command, this, _1, _2));

      } else if(is_connected) {
        std::string command = "set runmode stop;";
        boost::system::error_code ec;
        command_socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }

        command_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }
        command_socket.close(ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }

        waveform_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }
        waveform_socket.close(ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }

        is_connected = false;
      }
    } else if (key_str == "Channels") {
      channels = std::get<ObservableListPtr>(v);
    }
  }

  void on_receive_command(const boost::system::error_code& error, size_t length) {
    if(error) {
      THALAMUS_LOG(error) << error.what();
      (*state)["Running"].assign(false);
      return;
    }
    auto text = std::string(reinterpret_cast<char*>(command_buffer), length);
    std::cout << text;
    if(getting_sample_rate) {
      sample_rate_response += text;
    }

    command_socket.async_receive(boost::asio::buffer(command_buffer, sizeof(command_buffer)), std::bind(&Impl::on_receive_command, this, _1, _2));
  }

  enum class WaveParserState {
    MAGIC_NUMBER,
    FRAME_TIMESTAMP,
    FRAME_SAMPLES
  };
  WaveParserState wave_parser_state = WaveParserState::MAGIC_NUMBER;

  void on_receive_waveform(const boost::system::error_code& error, size_t length) {
    if(error) {
      THALAMUS_LOG(error) << error.what();
      (*state)["Running"].assign(false);
      return;
    }
    time = std::chrono::steady_clock::now().time_since_epoch();

    size_t i = 0;
    size_t remaining = length;
    unsigned int magic;
    auto parsing = true;
    while(parsing) {
      switch(wave_parser_state) {
        case WaveParserState::MAGIC_NUMBER:
          if(length - i >= 4) {
            magic = boost::endian::little_to_native(*reinterpret_cast<unsigned int*>(waveform_buffer + i));
            i += 4;
            if(magic != 0x2ef07a08) {
              if(error) {
                THALAMUS_LOG(error) << "Magic number check failed";
                (*state)["Running"].assign(false);
                return;
              }
            }
            frame = 0;
            wave_parser_state = WaveParserState::FRAME_TIMESTAMP;
          } else {
            parsing = false;
          }
          break;
        case WaveParserState::FRAME_TIMESTAMP:
          if(length - i >= 4) {
            timestamp = boost::endian::little_to_native(*reinterpret_cast<unsigned int*>(waveform_buffer + i));
            data[0].push_back(timestamp);
            i += 4;
            channel = 0;
            wave_parser_state = WaveParserState::FRAME_SAMPLES;
          } else {
            parsing = false;
          }
          break;
        case WaveParserState::FRAME_SAMPLES:
          if(length - i >= 2) {
            data[channel+1].push_back(boost::endian::little_to_native(*reinterpret_cast<unsigned short*>(waveform_buffer + i)));
            i += 2;
            ++channel;
            if(channel == num_channels) {
              channel = 0;
              ++frame;
              wave_parser_state = frame == 128 ? WaveParserState::MAGIC_NUMBER : WaveParserState::FRAME_TIMESTAMP;
            }
          } else {
            parsing = false;
          }
          break;
      }
    }

    num_samples = std::accumulate(data.begin(), data.end(), std::numeric_limits<size_t>::max(), [](size_t a, auto& b) { return std::min(a, b.size()); });
    if(num_samples > 0) { 
      outer->ready(outer);
    }
    std::copy(waveform_buffer+i, waveform_buffer+length, waveform_buffer);
    waveform_socket.async_receive(boost::asio::buffer(waveform_buffer, sizeof(waveform_buffer)), std::bind(&Impl::on_receive_waveform, this, _1, _2));
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
