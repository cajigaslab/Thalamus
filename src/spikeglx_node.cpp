#include <spikeglx_node.hpp>
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
#include <base_node.hpp>
#include <absl/strings/str_split.h>
#include <state.hpp>
#include <boost/signals2.hpp>
#include <modalities.h>
#include <thread_pool.h>
#include <thalamus/atoi.h>

using namespace thalamus;
using namespace std::chrono_literals;
using namespace std::placeholders;

#define SPIKEGLX_ASSERT(cond, message) if(!(cond)) {handle_error(message);return;}
#define BUFFER_SIZE 16777216

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
  unsigned char buffer[BUFFER_SIZE];
  size_t buffer_length = 0;
  size_t buffer_offset = 0;
  size_t buffer_total = 0;
  std::vector<std::string_view> lines;
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
  size_t position = 0;
  size_t complete_samples;
  std::chrono::nanoseconds time;
  bool skip_offset = false;
  std::vector<std::pair<std::string, std::function<void()>>> spikeglx_queue;
  bool queue_busy = false;
  ThreadPool& pool;

  enum class FetchState {
    READ_HEADER,
    READ_DATA,
    READ_OK
  };

  enum class Device {
    IMEC, NI
  };
  std::map<std::pair<Device, int>, size_t> sample_counts;
  std::map<std::pair<Device, int>, std::chrono::nanoseconds> sample_intervals;

  static const char* device_to_string(Device d) {
    switch(d) {
      case Device::IMEC:
        return "IMEC";
      case Device::NI:
        return "NI";
      default:
        THALAMUS_ASSERT(false, "Invalid Device");
    }
  }
  static const char* state_to_string(FetchState d) {
    switch (d) {
    case FetchState::READ_HEADER:
      return "READ_HEADER";
    case FetchState::READ_DATA:
      return "READ_DATA";
    case FetchState::READ_OK:
      return "READ_OK";
    default:
      THALAMUS_ASSERT(false, "Invalid FetchState");
    }
  }

  std::vector<std::vector<double>> ni_data;
  std::vector<std::vector<std::vector<double>>> imec_data;
  std::vector<std::string> ni_names;
  std::vector<std::vector<std::string>> imec_names;
  std::atomic_int pending_bands = 0;
  std::chrono::steady_clock::time_point fetch_start;
  double latency = 0;
  Device current_js;
  int current_ip;

  void on_fetch(Device js, int ip, FetchState fetch_state, std::function<void()> callback) {
    //THALAMUS_LOG(info) << "on_fetch " << device_to_string(js) << " " << ip << " " << state_to_string(fetch_state);
    auto& data = js == Device::IMEC ? imec_data[ip] : ni_data;
    auto& names = js == Device::IMEC ? imec_names[ip] : ni_names;
    auto& sample_count = sample_counts[std::make_pair(js, ip)];
    auto prefix = (js == Device::IMEC ? "IMEC:" : "NI:") + std::to_string(ip) + ":";
    switch(fetch_state) {
      case FetchState::READ_HEADER: {
        char* chars = reinterpret_cast<char*>(buffer);
        for(auto i = buffer_offset;i < buffer_offset+buffer_length;++i) {
          if(chars[i] == '\n') {
            chars[i] = 0;
            //THALAMUS_LOG(info) << "fetch header " << chars;
            if(std::string_view(chars, chars+i).starts_with("ERROR FETCH: No data")) {
              for (auto& d : data) {
                d.clear();
              }
              process_queue();
              callback();
              return;
            }
            auto count = sscanf(chars, "BINARY_DATA %d %d uint64(%llu)", &nchans, &nsamples, &from_count);
            SPIKEGLX_ASSERT(count == 3, std::string("Failed to read BINARY_DATA header: ") + chars);
            data.resize(nchans);
            for (auto& d : data) {
              d.clear();
            }
            names.resize(nchans);
            for(size_t i = 0;i < names.size();++i) {
              auto& name = names[i];
              if(name.empty()) {
                name = prefix + std::to_string(i);
              }
            }
            samples_read = 0;
            next_channel = 0;
            complete_samples = 0;

            buffer_offset = i+1;
            buffer_length = buffer_total - buffer_offset;
            skip_offset = true;
            on_fetch(js, ip, FetchState::READ_DATA, callback);
            return;
          }
        }
        fill_buffer(std::bind(&Impl::on_fetch, this, js, ip, FetchState::READ_HEADER, callback));
      }
      break;
      case FetchState::READ_DATA: {
        size_t position = 0;

        unsigned char* bytes = buffer + (skip_offset ? buffer_offset : 0);

        size_t end = skip_offset ? buffer_length : buffer_total;

        //THALAMUS_LOG(info) << "fetch data " << position_shorts << " " << end_shorts;
        //for(auto& d : data) {
        //  d.erase(d.begin(), d.begin() + complete_samples);
        //}
        //short lower = std::numeric_limits<short>::max();
        //short upper = std::numeric_limits<short>::min();
        auto band_size = std::max(1u, nchans/pool.num_threads);
        band_size += (nchans % band_size) ? 1 : 0;

        pending_bands = nchans/band_size;
        pending_bands += (nchans % band_size) ? 1 : 0;

        auto post_deinterlace = [&,js,ip,callback,end] {
          //THALAMUS_LOG(info) << "post_deinterlace";
          samples_read += end / sizeof(short);
          
          if(samples_read / nchans == nsamples) {
            auto now = std::chrono::steady_clock::now();
            time = now.time_since_epoch();
            latency = (now - fetch_start).count()/1e6;
            complete_samples = std::numeric_limits<size_t>::max();
            for (auto& d : data) {
              complete_samples = std::min(complete_samples, d.size());
            }
            auto last_num_channels = num_channels;
            num_channels = ni_data.size();
            for(auto& d : imec_data) {
              num_channels += d.size();
            }
            if(num_channels != last_num_channels) {
              outer->channels_changed(outer);
            }
            if (complete_samples > 0) {
              current_js = js;
              current_ip = ip;
              outer->ready(outer);
            }
            //THALAMUS_LOG(info) << "fetch done";
            sample_count += nsamples;
            on_fetch(js, ip, FetchState::READ_OK, callback);
            return;
          }

          consume_buffer(buffer_total - (end % 2));
          skip_offset = false;
          fill_buffer(std::bind(&Impl::on_fetch, this, js, ip, FetchState::READ_DATA, callback));
        };
        for(auto c = 0;c < nchans;c+=band_size) {
          pool.push([&,c,post_deinterlace,band_size,end,bytes] {
            for(auto subc = 0;subc < band_size && c+subc < nchans;++subc) {
              auto channel = (samples_read+c+subc) % nchans;
              for(auto i = 2*(c+subc);i+1 < end;i += 2*nchans) {
                short sample = bytes[i] + (bytes[i+1] << 8);
                data[channel].push_back(sample);
              }
            }
            if(--pending_bands == 0) {
              boost::asio::post(io_context, post_deinterlace);
            }
          });
        }
      }
      break;
      case FetchState::READ_OK: {
        char* chars = reinterpret_cast<char*>(buffer);
        auto end = chars + buffer_total;
        if(buffer_total >= 3 && std::string_view(end-3, end) == "OK\n") {
          process_queue();
          callback();
          return;
        }
        
        fill_buffer(std::bind(&Impl::on_fetch, this, js, ip, FetchState::READ_OK, callback));
      }
      break;
    }
  }

  void fetch(Device js, int ip, const std::string& subset, std::function<void()> callback) {
    //THALAMUS_LOG(info) << "fetch " << device_to_string(js) << " " << ip << " " << subset;
    auto i = sample_counts.find(std::make_pair(js, ip));
    auto j = sample_intervals.find(std::make_pair(js, ip));
    if(i == sample_counts.end()) {
      std::string command;
      if(js == Device::IMEC) {
        if(spike_glx_version < 20240000) {
          command = absl::StrFormat("GETSCANCOUNT %d", 0);
        } else {
          command = absl::StrFormat("GETSTREAMSAMPLECOUNT %d 0", 2);
        }
      } else {
        if(spike_glx_version < 20240000) {
          command = absl::StrFormat("GETSCANCOUNT %d", -1);
        } else {
          command = absl::StrFormat("GETSTREAMSAMPLECOUNT %d 0", 0);
        }
      }

      query_string(command, [this,js,ip,subset,callback](auto& text) {
        //THALAMUS_LOG(info) << "scancount " << text;
        size_t count;
        auto success = absl::SimpleAtoi(text, &count);
        sample_counts[std::make_pair(js, ip)] = count;
        SPIKEGLX_ASSERT(success, std::string("Failed to parse sample count: ") + text);
        fetch(js, ip, subset, callback);
      });
    } else if (j == sample_intervals.end()) {
      std::string command;
      if (js == Device::IMEC) {
        if (spike_glx_version < 20240000) {
          command = absl::StrFormat("GETSAMPLERATE %d", 0);
        }
        else {
          command = absl::StrFormat("GETSTREAMSAMPLERATE %d 0", 2);
        }
      }
      else {
        if (spike_glx_version < 20240000) {
          command = absl::StrFormat("GETSAMPLERATE %d", -1);
        }
        else {
          command = absl::StrFormat("GETSTREAMSAMPLERATE %d 0", 0);
        }
      }

      query_string(command, [this, js, ip, subset, callback](auto& text) {
        //THALAMUS_LOG(info) << "samplerate " << text;
        double rate;
        auto success = absl::SimpleAtod(text, &rate);
        sample_intervals[std::make_pair(js, ip)] = std::chrono::nanoseconds(size_t(1000000000/rate));
        SPIKEGLX_ASSERT(success, std::string("Failed to parse sample rate: ") + text);
        fetch(js, ip, subset, callback);
      });
    } else {
      fetch_start = std::chrono::steady_clock::now();
      std::string command;
      if(js == Device::IMEC) {
        if(spike_glx_version < 20240000) {
          command = absl::StrFormat("FETCH %d %d 50000 %s", 0, i->second, subset);
        } else {
          command = absl::StrFormat("FETCH %d 0 %d 50000 %s", 2, i->second, subset);
        }
      } else {
        if(spike_glx_version < 20240000) {
          command = absl::StrFormat("FETCH %d %d 50000 %s", -1, i->second, subset);
        } else {
          command = absl::StrFormat("FETCH %d 0 %d 50000 %s", 0, i->second, subset);
        }
      }
      
      enqueue(command, std::bind(&Impl::on_fetch, this, js, ip, FetchState::READ_HEADER, callback));
    }
  }
  
  void on_query_string(std::function<void(const std::string&)> callback) {
    char* chars = reinterpret_cast<char*>(buffer);
    auto end = chars + buffer_total;
    if(buffer_total >= 3 && std::string_view(end-3, end) == "OK\n") {
      std::string_view view(chars, end);
      auto offset = view.find('\n');
      std::string result(chars, chars+offset);
      //THALAMUS_LOG(info) << "on_query_string " << result;
      process_queue();
      callback(result);
      return;
    }
    
    fill_buffer(std::bind(&Impl::on_query_string, this, callback));
  }

  void query_string(const std::string& command, std::function<void(const std::string&)> callback) {
    enqueue(command, std::bind(&Impl::on_query_string, this, callback));
  }

  void consume_buffer(size_t count = BUFFER_SIZE) {
    if(count < buffer_offset + buffer_length) {
      std::copy(buffer + count, buffer + buffer_offset + buffer_length, buffer);
      buffer_offset = buffer_offset + buffer_length - count;
      buffer_length = 0;
    } else {
      buffer_offset = 0;
      buffer_length = 0;
    }
    buffer_total = buffer_offset + buffer_length;
  }

  void on_fill_buffer(std::function<void()> callback, const boost::system::error_code& ec, size_t length) {
    SPIKEGLX_ASSERT(!ec, ec);
    if(!queue_busy) {
      return;
    }
    buffer_length = length;
    buffer_total = buffer_offset + buffer_length;
    callback();
  }

  void fill_buffer(std::function<void()> callback) {
    buffer_offset += buffer_length;
    //THALAMUS_LOG(info) << buffer_offset << " " << (sizeof(buffer) - buffer_offset);
    socket.async_receive(boost::asio::buffer(buffer + buffer_offset, sizeof(buffer) - buffer_offset), std::bind(&Impl::on_fill_buffer, this, callback, _1, _2));
  }

  void process_queue() {
    queue_busy = false;
    //THALAMUS_LOG(info) << "queue free";
    if(spikeglx_queue.empty()) {
      return;
    }
    auto& pair = spikeglx_queue.front();
    std::string command_nl = pair.first + "\n";
    boost::system::error_code ec;
    socket.send(boost::asio::const_buffer(command_nl.data(), command_nl.size()), 0, ec);
    SPIKEGLX_ASSERT(!ec, ec);

    //THALAMUS_LOG(info) << "process_queue " << pair.first;
    consume_buffer();
    fill_buffer(pair.second);
    spikeglx_queue.erase(spikeglx_queue.begin());
    queue_busy = true;
    //THALAMUS_LOG(info) << "queue busy";
  }

  void enqueue(const std::string& command, std::function<void()> callback) {
    //THALAMUS_LOG(info) << "enqueue " << queue_busy << " " << command;
    spikeglx_queue.emplace_back(command, callback);
    if(!queue_busy) {
      process_queue();
    }
  }

  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, SpikeGlxNode* outer)
    : state(state)
    , io_context(io_context)
    , timer(io_context)
    , socket(io_context)
    , outer(outer)
    , pool(graph->get_thread_pool()) {
    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    (*state)["Running"].assign(false);
    memset(&address, 0, sizeof(address));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false);
  }

  void handle_error(const std::string& message, bool do_disconnect = true) {
    THALAMUS_LOG(error) << message;
    (*state)["Error"].assign(message);
    queue_busy = false;
    //THALAMUS_LOG(info) << "error free";
    if(do_disconnect) {
      disconnect();
    }
  }

  void handle_error(const boost::system::error_code& ec, bool do_disconnect = true) {
    handle_error(ec.what(), do_disconnect);
  }

  void disconnect() {
    if(!is_connected) {
      return;
    }
    boost::system::error_code ec;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    handle_error(ec, false);
    socket.close(ec);
    handle_error(ec, false);
    is_connected = false;
    is_running = false;
    (*state)["Running"].assign(false);
  }

  void connect(std::function<void()> callback) {
    if(is_connected) {
      callback();
    }

    SPIKEGLX_ASSERT(state->contains("Address"), "No Address defined");

    std::string address_str = state->at("Address");
    std::vector<std::string> address_tokens = absl::StrSplit(address_str, ':');
    if (address_tokens.size() == 1) {
      address_tokens.push_back("4142");
    }

    SPIKEGLX_ASSERT(address_tokens.size() == 2, std::string("Failed to parse address :") + address_str);

    boost::asio::ip::tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(address_tokens.at(0), address_tokens.at(1));
    boost::system::error_code ec;
    boost::asio::async_connect(socket, endpoints, std::bind(&Impl::on_connect, this, callback, _1));
  }

  void on_connect(std::function<void()> callback, const boost::system::error_code& e) {
    SPIKEGLX_ASSERT(!e, e);
    load_version(callback);
  }

  void load_version(std::function<void()> callback) {
    query_string("GETVERSION",  [&, callback](auto& text) {
      //THALAMUS_LOG(info) << text;
      std::vector<std::string_view> tokens = absl::StrSplit(text, absl::ByAnyChar(".,"));
      SPIKEGLX_ASSERT(tokens.size() >= 2, std::string("Failed to parse SpikeGLX version: ") + text);

      auto success = absl::SimpleAtoi(tokens[1], &spike_glx_version);
      SPIKEGLX_ASSERT(success, std::string("Failed to parse SpikeGLX version :") + text);

      is_connected = true;
      count_probes(callback);
    });
  }

  void on_fetch_space(const boost::system::error_code& ec) {
    fetch_continuously();
  }

  void fetch_continuously() {
    if(!is_running) {
      return;
    }
    //THALAMUS_LOG(info) << "fetch_continuously " << device_to_string(js) << " " << ip;
    for(auto i = 0;i < imec_count;++i) {
      fetch(Device::IMEC, i, imec_subsets[i], [] {});
    }
    fetch(Device::NI, 0, "", [&] {
      //fetch_continuously(js, ip, subset);
      timer.expires_after(2ms);
      timer.async_wait(std::bind(&Impl::on_fetch_space, this, _1));
    });
  }

  long long imec_count = 0;
  long long imec_pending = 0;

  std::vector<std::string> imec_subsets;

  void count_probes(std::function<void()> callback) {
    auto command = spike_glx_version < 20240000 ? "GETIMPROBECOUNT" : "GETSTREAMNP 2";
    query_string(command, [&,callback](const auto& text) {
      auto callback_wrapper = [&,callback] () {
        if(--imec_pending == 0) {
          callback();
        }
      };

      imec_count = parse_number<long long>(text);
      imec_data.resize(imec_count);
      imec_names.resize(imec_count);

      imec_pending = 1;
      (*state)["imec_count"].assign(imec_count, callback_wrapper);

      for(auto i = 0ll;i < imec_count;++i) {
        auto text = absl::StrFormat("imec_subset_%d", i);
        if(state->contains(text)) {
          continue;
        }

        ++imec_pending;
        (*state)[text].assign("*", callback_wrapper);
      }
    });
  }

  void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
    auto key_str = std::get<std::string>(k);
    if(key_str.starts_with("imec_subset")) {
      std::vector<std::string> tokens = absl::StrSplit(key_str, '_');
      auto index = parse_number<size_t>(tokens.back());
      imec_subsets.resize(index+1, "*");
      imec_subsets[index] = std::get<std::string>(v);
    } else if (key_str == "Running") {
      is_running = std::get<bool>(v);
      if (is_running) {
        sample_counts.clear();
        connect([&] {
          fetch_continuously();
        });
      }
      else {
        disconnect();
      }
    }
  }

  size_t spike_glx_version;
};

SpikeGlxNode::SpikeGlxNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
  : impl(new Impl(state, io_context, graph, this)) {}

SpikeGlxNode::~SpikeGlxNode() {}
  
std::span<const double> SpikeGlxNode::data(int channel) const {
  if(channel == 0) {
    return std::span<const double>(&impl->latency, &impl->latency+1);
  }
  --channel;

  for(auto j = 0ull;j < impl->imec_data.size();++j) {
    auto& channels = impl->imec_data[j];
    if(channel < channels.size()) {
      if(impl->current_js == Impl::Device::IMEC && impl->current_ip == j) {
        return std::span<const double>(channels[channel].begin(), channels[channel].begin() + impl->complete_samples);
      } else {
        return std::span<const double>();
      }
    }
    channel -= channels.size();
  }

  if(channel < impl->ni_data.size() && impl->current_js == Impl::Device::NI) {
    return std::span<const double>(impl->ni_data[channel].begin(), impl->ni_data[channel].begin() + impl->complete_samples);
  }

  return std::span<const double>();
}

std::string_view SpikeGlxNode::name(int channel) const {
  if(channel == 0) {
    return "Latency (ms)";
  }
  --channel;

  for(const auto& names : impl->imec_names) {
    if(channel < names.size()) {
      return names[channel];
    }
    channel -= names.size();
  }

  if(channel < impl->ni_data.size()) {
    return impl->ni_names[channel];
  }
  
  return "";
}

int SpikeGlxNode::num_channels() const {
  return impl->num_channels+1;
}

std::chrono::nanoseconds SpikeGlxNode::sample_interval(int i) const {
  if(i == 0) {
    return 0ns;
  }
  --i;

  for(auto j = 0ull;j < impl->imec_data.size();++j) {
    auto& channels = impl->imec_data[j];
    if(i < channels.size()) {
      return impl->sample_intervals[std::make_pair(Impl::Device::IMEC, int(j))];
    }
    i -= channels.size();
  }

  if(i < impl->ni_data.size()) {
    return impl->sample_intervals[std::make_pair(Impl::Device::NI, 0)];
  }
  
  return 0ns;
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

boost::json::value SpikeGlxNode::process(const boost::json::value&) {
  impl->connect([]{});
  return boost::json::value();
  //impl->connect([&] {
  //  auto command = impl->spike_glx_version < 20240000 ? "GETIMPROBECOUNT" : "GETSTREAMNP";
  //  impl->query_string(command, [&](const auto& text) {
  //    auto count = parse_number<int>(text);
  //    impl->imec_data.resize(count);
  //    (*impl->state)["imec_count"].assign(count);
  //    for(auto i = 0;i < count:++i) {
  //      std::string command;
  //      if(impl->spike_glx_version < 20240000) {
  //        command = absl::StrFormat("GETACQCHANCOUNTS %d", i);
  //      } else {
  //        command = absl::StrFormat("GETSTREAMACQCHANS 2 %d", i);
  //      }
  //      impl->query_string(command, [&,i](const auto& text) {
  //        auto tokens = absl::StrSplit(text, ' ');
  //        auto sum = 0;
  //        for(auto token : tokens) {
  //          sum += parse_number<int>(token);
  //        }
  //        impl->imec_data[i].resize(sum);
  //      })
  //    }
  //  });
  //});
}
