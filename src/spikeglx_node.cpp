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

struct SpikeGlxNode::Impl {
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
  std::vector<std::string_view> lines;
  std::vector<std::string> names;
  SpikeGlxNode* outer;
  bool is_running = false;
  bool is_connected = false;
  double sample_rate = 0;
  std::chrono::nanoseconds sample_interval;
  size_t sample_count = 0;
  int nchans;
  int nsamples;
  int next_channel = 0;
  size_t samples_read;
  size_t from_count;
  std::vector<std::vector<double>> data;
  size_t position = 0;
  size_t complete_samples;
  std::chrono::nanoseconds time;
  bool skip_offset = false;

  enum class FetchState {
    READ_HEADER,
    READ_DATA,
    READ_OK
  } fetch_state;

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

        lines.clear();
        std::string command = "GETSAMPLERATE -1\n";
        socket.send(boost::asio::const_buffer(command.data(), command.size()), 0, ec);
        if(ec) {
          THALAMUS_LOG(error) << ec.what();
          (*state)["Running"].assign(false);
          return;
        }
        is_connected = true;
        read_string(std::bind(&Impl::on_sample_rate, this, _1));
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

  void on_binary_wait(const boost::system::error_code& error) {
    if(error) {
      THALAMUS_LOG(error) << error.what();
      (*state)["Running"].assign(false);
      return;
    }
    fetch();
  }

  void on_binary_data(const boost::system::error_code& error, size_t length) {
    if(error) {
      THALAMUS_LOG(error) << error.what();
      (*state)["Running"].assign(false);
      return;
    }
    switch(fetch_state) {
      case FetchState::READ_HEADER: {
        char* chars = reinterpret_cast<char*>(buffer);
        for(auto i = buffer_offset;i < buffer_offset+length;++i) {
          if(chars[i] == '\n') {
            chars[i] = 0;
            //THALAMUS_LOG(info) << "fetch header " << chars;
            if(std::string_view(chars, chars+i).starts_with("ERROR FETCH: No data")) {
              //THALAMUS_LOG(info) << "fetch retry";
              timer.expires_after(1ms);
              timer.async_wait(std::bind(&Impl::on_binary_wait, this, _1));
              return;
            }
            auto count = sscanf(chars, "BINARY_DATA %d %d uint64(%llu)", &nchans, &nsamples, &from_count);
            if(count < 3) {
              THALAMUS_LOG(error) << "Failed to read BINARY_DATA header: " << chars;
              (*state)["Running"].assign(false);
              return;
            }
            data.resize(nchans);
            for (auto& d : data) {
              d.clear();
            }
            names.resize(nchans);
            for(size_t i = 0;i < names.size();++i) {
              auto& name = names[i];
              if(name.empty()) {
                name = std::to_string(i);
              }
            }
            samples_read = 0;
            next_channel = 0;
            complete_samples = 0;

            fetch_state = FetchState::READ_DATA;
            length -= i+1 - buffer_offset;
            buffer_offset = i+1;
            skip_offset = true;
            on_binary_data(error, length);
            return;
          }
        }
        buffer_offset += length;
        socket.async_receive(boost::asio::buffer(buffer + buffer_offset, sizeof(buffer) - buffer_offset), std::bind(&Impl::on_binary_data, this, _1, _2));
      }
      case FetchState::READ_DATA: {
        size_t position_shorts = 0;

        unsigned char* bytes = buffer + (skip_offset ? buffer_offset : 0);
        short* shorts = reinterpret_cast<short*>(bytes);

        size_t end_shorts = (length + (skip_offset ? 0 : buffer_offset)) / sizeof(short);
        size_t end_bytes = end_shorts * sizeof(short);

        //THALAMUS_LOG(info) << "fetch data " << position_shorts << " " << end_shorts;
        for(auto& d : data) {
          d.erase(d.begin(), d.begin() + complete_samples);
        }
        while(position_shorts < end_shorts) {
          data[samples_read++ % nchans].push_back(shorts[position_shorts++]);
        }
        time = std::chrono::steady_clock::now().time_since_epoch();
        complete_samples = samples_read/nchans;
        outer->ready(outer);

        if(complete_samples == nsamples) {
          //THALAMUS_LOG(info) << "fetch done";
          sample_count += nsamples;
          fetch_state = FetchState::READ_OK;
          on_binary_data(error, length);
          return;
        }
        size_t position_bytes = position_shorts*sizeof(short);
        std::copy(bytes + position_bytes, bytes + end_bytes, buffer);
        buffer_offset = end_bytes - position_bytes;
        skip_offset = false;
        socket.async_receive(boost::asio::buffer(buffer + buffer_offset, sizeof(buffer) - buffer_offset), std::bind(&Impl::on_binary_data, this, _1, _2));
      }
      break;
      case FetchState::READ_OK: {
        char* chars = reinterpret_cast<char*>(buffer);
        auto end = chars + buffer_offset + length;
        //THALAMUS_LOG(info) << "fetch READ_OK " << end - chars;
        //if (end - chars >= 3) {
        //  THALAMUS_LOG(info) << "fetch READ_OK2 " << std::string_view(end - 3, end) << (std::string_view(end - 3, end) == "OK\n");
        //}
        if(end - chars >= 3 && std::string_view(end-3, end) == "OK\n") {
          fetch();
          return;
        }
        buffer_offset += length;
        socket.async_receive(boost::asio::buffer(buffer + buffer_offset, sizeof(buffer) - buffer_offset), std::bind(&Impl::on_binary_data, this, _1, _2));
      }
      break;
    }
  }

  void fetch() {
    boost::system::error_code ec;
    std::stringstream command;
    command << "FETCH -1 " << sample_count << " 50000\n";
    //THALAMUS_LOG(info) << "fetch " << command.str();
    socket.send(boost::asio::const_buffer(command.str().data(), command.str().size()), 0, ec);
    if(ec) {
      THALAMUS_LOG(error) << ec.what();
      (*state)["Running"].assign(false);
      return;
    }
    buffer_offset = 0;
    fetch_state = FetchState::READ_HEADER;
    socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::on_binary_data, this, _1, _2));
  }

  void on_sample_count(const std::string_view& text) {
    //THALAMUS_LOG(info) << "on_sample_count " << text;
    auto success = absl::SimpleAtoi(text, &sample_count);
    if(!success) {
      THALAMUS_LOG(error) << "Failed to parse sample count";
      (*state)["Running"].assign(false);
      return;
    }
    fetch();
  }

  void on_sample_rate(const std::string_view& text) {
    //THALAMUS_LOG(info) << "on_sample_rate " << text;
    auto success = absl::SimpleAtod(text, &sample_rate);
    if(!success) {
      THALAMUS_LOG(error) << "Failed to parse sample rate";
      (*state)["Running"].assign(false);
      return;
    }
    sample_interval = std::chrono::nanoseconds(size_t(1e9/sample_rate));
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

  void read_string(std::function<void(const std::string_view&)> callback) {
    lines.clear();
    buffer_length = 0;
    buffer_offset = 0;
    socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::read_string_receive, this, callback, _1, _2));
  }

  void read_string_receive(std::function<void(const std::string_view&)> callback, const boost::system::error_code& error, size_t length) {
    if(error) {
      THALAMUS_LOG(error) << error.what();
      (*state)["Running"].assign(false);
      return;
    }

    char* chars = reinterpret_cast<char*>(buffer);
    std::string_view view(chars, chars + buffer_offset + length);
    size_t offset = buffer_offset;
    size_t last_offset = buffer_offset;
    while((offset = view.find('\n', offset)) != std::string::npos) {
      lines.emplace_back(chars + last_offset, chars + offset);
      ++offset;
      last_offset = offset;
    }
    if(!lines.empty() && lines.back() == "OK") {
      callback(lines.front());
      return;
    }

    buffer_offset += length;

    socket.async_receive(boost::asio::buffer(buffer + buffer_offset, sizeof(buffer) - buffer_offset), std::bind(&Impl::read_string_receive, this, callback, _1, _2));
  }
};

SpikeGlxNode::SpikeGlxNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
  : impl(new Impl(state, io_context, graph, this)) {}

SpikeGlxNode::~SpikeGlxNode() {}
  
std::span<const double> SpikeGlxNode::data(int channel) const {
  return std::span<const double>(impl->data[channel].begin(), impl->data[channel].begin() + impl->complete_samples);
}

std::string_view SpikeGlxNode::name(int channel) const {
  return impl->names[channel];
}

int SpikeGlxNode::num_channels() const {
  return impl->data.size();
}

std::chrono::nanoseconds SpikeGlxNode::sample_interval(int i) const {
  return impl->sample_interval;
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
