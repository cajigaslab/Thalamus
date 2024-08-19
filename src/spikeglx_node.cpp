#include <spikeglx_node.h>
#include <boost/asio.hpp>
#include <vector>
#include <map>
#include <functional>
#include <string>
#include <iostream>
#include <variant>
#include <regex>
#include <thread>
//#include <plot.h>
#include <base_node.h>
#include <absl/strings/str_split.h>
#include <state.h>
#include <boost/signals2.hpp>
#include <modalities.h>

using namespace thalamus;
using namespace std::chrono_literals;
using namespace std::placeholders;

class SpikeGlxNode::Impl {
  ObservableDictPtr state;
  size_t observer_id;
  boost::signals2::scoped_connection state_connection;
  boost::asio::io_context& io_context;
  boost::asio::high_resolution_timer timer;
  boost::asio::ip::tcp::socket socket;
  size_t num_channels;
  size_t buffer_size;
  std::vector<short> short_buffer;
  std::vector<double> double_buffer;
  std::vector<int> channels;
  std::map<size_t, std::function<void(Node*)>> observers;
  //double sample_rate;
  size_t counter = 0;
  int address[6];
  unsigned char buffer[1048576];
  size_t buffer_length = 0;
  size_t buffer_offset = 0;
  std::vector<std::string> lines;
  SpikeGlxNode* outer;
  bool is_running = false;
  bool is_connected = false;
  double sample_rate = 0;
  std::chrono::nanoseconds sample_interval;
  size_t sample_count = 0;
  size_t nchans;
  size_t nsamples;
  size_t samples_read;
  size_t from_count;
  std::vector<std::vector<double>> data;
  size_t position = 0;
  std::chrono::nanoseconds time;

  enum class FetchState {
    READ_HEADER,
    READ_DATA
  } fetch_state;
public:
  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, SpikeGlxNode* outer)
    : state(state)
    , io_context(io_context)
    , timer(io_context)
    , socket(io_context)
    , outer(outer) {
    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    (*state)["Running"].assign(false);
    memset(&address, 0, sizeof(address));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false);
  }

  void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Running") {
      is_running = std::get<bool>(v);
      if (is_running) {
        std::string address_str = state->at("Address");
        std::vector<std::string> address_tokens = absl::StrSplit(address_str, ':');
        if (address_tokens.size() < 2) {
          address_tokens.push_back("4142");
        }
        boost::asio::ip::tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(address_tokens.at(0), address_tokens.at(1));
        boost::system::error_code ec;
        boost::asio::connect(socket, endpoints, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
          (*state)["Running"].assign(false);
          return;
        }

        stream_state = GET_SAMPLE_RATE;
        lines.clear();
        std::string command = "GETSAMPLERATE -1\n";
        socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
          (*state)["Running"].assign(false);
          return;
        }
        auto callback = std::bind(&Impl::on_sample_rate);
        read_string(std::bind(&Impl::on_sample_rate, this, _1));
        socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::read_string, this, _1, _2, callback));
        is_connected = true;
      } else if(is_connected) {
        boost::system::error_code ec;
        socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }
        socket.close(ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
        }
        is_connected = false;
      }
    }
  }

  void on_binary_data(const boost::system::error_code& error, size_t length) {
    if(error) {
      THALAMUS_LOG(error) << error.what();
      (*state)["Running"].assign(false);
      return;
    }
    switch(fetch_state) {
      case READ_HEADER: {
        char* chars = reinterpret_cast<char*>(buffer);
        for(auto i = buffer_offset;i < buffer_offset+length;++i) {
          if(chars[i] == '\n') {
            std::string_view header(chars, chars+i-1);
            auto tokens = absl::StrSplit(header, ' ');
            if(tokens.size() < 4) {
              THALAMUS_LOG(error) << "Failed to read BINARY_DATA header: " << header;
              (*state)["Running"].assign(false);
              return;
            }
            auto success = absl::SimpleAtoi(tokens[1], &nchans);
            if(!success) {
              THALAMUS_LOG(error) << "Failed to read BINARY_DATA nchans: " << header;
              (*state)["Running"].assign(false);
              return;
            }
            data.resize(nchans);
            samples_read = 0;
            success = absl::SimpleAtoi(tokens[2], &nsamples);
            if(!success) {
              THALAMUS_LOG(error) << "Failed to read BINARY_DATA nsamples: " << header;
              (*state)["Running"].assign(false);
              return;
            }
            success = absl::SimpleAtoi(tokens[3], &from_count);
            if(!success) {
              THALAMUS_LOG(error) << "Failed to read BINARY_DATA fromCt: " << header;
              (*state)["Running"].assign(false);
              return;
            }

            fetch_state = READ_DATA;
            length -= i+1 - buffer_offset;
            buffer_offset = i+1;
            on_binary_data(error, length);
          }
        }
        break;
      }
      case READ_DATA: {
        position = 0;
        short* shorts = reinterpret_cast<short*>(buffer);
        size_t end = (buffer_offset + length) / sizeof(short);
        for(auto& d : data) {
          d.clear();
        }
        while(position + nchans < end) {
          for(auto i = 0;i < nchans;++i) {
            data[i].push_back(shorts[position++]);
          }
          ++samples_read;
        }
        time = std::chrono::steady_clock::now();
        outer->ready(outer);

        if(samples_read == nsamples) {
          fetch();
          return;
        }
        std::copy(position*sizeof(short), end*sizeof(short), buffer);
        buffer_offset = (end - position)*sizeof(short);
        socket.async_receive(boost::asio::buffer(buffer + buffer_offset, sizeof(buffer) - buffer_offset), std::bind(&Impl::on_binary_data, this, _1, _2));
      }
    }
  }

  void fetch() {
    boost::system::error_code ec;
    std::stringstream command;
    command << "FETCH 0 0 " << sample_count << "\n";
    socket.send(boost::asio::const_buffer(command.str().data(), command.str().size()), 0, ec);
    if(ec) {
      THALAMUS_LOG(error) << ec.what();
      (*state)["Running"].assign(false);
      return;
    }
    buffer_offset = 0;
    fetch_state = READ_HEADER;
    socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::on_binary_data, this, _1, _2));
  }

  void on_sample_count(const std::string& text) {
    auto success = absl::SimpleAtoi(text, &sample_count);
    if(!success) {
      THALAMUS_LOG(error) << "Failed to parse sample count";
      (*state)["Running"].assign(false);
      return;
    }
    fetch();
  }

  void on_sample_rate(const std::string& text) {
    auto success = absl::SimpleAtod(text, &sample_rate);
    if(!success) {
      THALAMUS_LOG(error) << "Failed to parse sample rate";
      (*state)["Running"].assign(false);
      return;
    }
    sample_interval = std::chrono::nanoseconds(1e9/sample_rate);
    boost::system::error_code ec;
    std::string command = "GETSCANCOUNT -1\n";
    socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec);
    if(ec) {
      THALAMUS_LOG(error) << ec.what();
      (*state)["Running"].assign(false);
      return;
    }
    read_string(std::bind(&Impl::on_sample_count, this, _1));
  }

  void read_string(std::function<void(const std::string&)> callback) {
    lines.clear();
    buffer_length = 0;
    buffer_offset = 0;
    socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::read_string, this, _1, _2));
  }

  void read_string(std::function<void(const std::string&)> callback, const boost::system::error_code& error, size_t length) {
    if(error) {
      THALAMUS_LOG(error) << error.what();
      (*state)["Running"].assign(false);
      return;
    }

    char* chars = reinterpret_cast<char*>(buffer);
    std::string_view view(chars, chars + buffer_offset + length);
    size_t offset = 0;
    size_t last_offset = 0;
    while((offset = view.find('\n', offset)) != std::string::npos) {
      lines.emplace_back(chars + last_offset, chars + offset);
      ++offset;
      last_offset = offset;
    }
    if(!lines.empty() && lines.back() == "OK") {
      callback(lines.front());
      return;
    }

    if(last_offset) {
      std::copy(buffer + last_offset, buffer + buffer_offset + length, buffer);
    }
    buffer_offset = buffer_offset + length - last_offset;

    socket.async_receive(boost::asio::buffer(buffer + buffer_offset, sizeof(buffer) - buffer_offset), std::bind(&Impl::read_string, this, _1, _2));
  }
};

SpikeGlxNode::SpikeGlxNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
  : impl(new Impl(state, io_context, graph, this)) {}

SpikeGlxNode::~SpikeGlxNode() {}
  
std::span<const double> SpikeGlxNode::data(int channel) const {
  return std::span<const double>(impl->data[channel].begin(), impl->data[channel].end());
}

std::string_view SpikeGlxNode::name(int channel) const {
  return std::string_view();
}

int SpikeGlxNode::num_channels() const {
  return impl->data.size();
}

std::chrono::nanoseconds SpikeGlxNode::sample_interval(int i) const {
  return impl->stample_interval;
}

std::chrono::nanoseconds SpikeGlxNode::time() const {
  return impl->time;
}

std::string SpikeGlxNode::type_name() {
  return "SPIKEGLX";
}

void SpikeGlxNode::inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) {}

size_t SpikeGlxNode::modalities() const {
  return THALAMUS_MODALITY_ANALOG;
}
