#include <thalamus/tracing.hpp>
#include <base_node.hpp>
#include <functional>
#include <map>
#include <modalities.h>
#include <spikeglx_node.hpp>
#include <state.hpp>
#include <string>
#include <thalamus/async.hpp>
#include <thalamus/atoi.h>
#include <thread_pool.hpp>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <absl/strings/str_split.h>
#include <boost/asio.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/signals2.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace thalamus;
using namespace std::chrono_literals;
using namespace std::placeholders;

#define BUFFER_SIZE 16777216

struct SpikeGlxNode::Impl {
  ObservableDictPtr state;
  ObservableDictPtr metadata_node;
  ObservableListPtr metadata_list;
  size_t observer_id;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection metadata_connection;
  boost::asio::io_context &io_context;
  boost::asio::high_resolution_timer timer;
  boost::asio::ip::tcp::socket socket;
  size_t num_channels;
  size_t buffer_size;
  std::vector<short> short_buffer;
  std::vector<double> double_buffer;
  std::vector<int> channels;
  std::map<size_t, std::function<void(Node *)>> observers;
  // double sample_rate;
  size_t counter = 0;
  int address[6];
  unsigned char buffer[BUFFER_SIZE];
  size_t buffer_length = 0;
  size_t buffer_offset = 0;
  size_t buffer_total = 0;
  std::vector<std::string_view> lines;
  SpikeGlxNode *outer;
  double sample_rate = 0;
  int nchans;
  int nsamples;
  int next_channel = 0;
  size_t samples_read;
  unsigned long long from_count;
  size_t position = 0;
  size_t complete_samples;
  std::chrono::nanoseconds time;
  bool skip_offset = false;
  std::vector<std::pair<std::string, std::function<void()>>> spikeglx_queue;
  bool queue_busy = false;
  ThreadPool &pool;

  enum class Device { IMEC, NI };
  std::map<std::pair<Device, int>, std::chrono::nanoseconds> sample_intervals;

  static const char *device_to_string(Device d) {
    switch (d) {
    case Device::IMEC:
      return "IMEC";
    case Device::NI:
      return "NI";
    }
  }

  std::vector<std::vector<short>> ni_data;
  std::vector<std::vector<std::vector<short>>> imec_data;
  std::vector<std::string> ni_names;
  std::vector<std::vector<std::string>> imec_names;
  std::chrono::steady_clock::time_point fetch_start;
  short latency = 0;
  Device current_js;
  int current_ip;
  // static unsigned long long io_track;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *graph, SpikeGlxNode *_outer)
      : state(_state), io_context(_io_context), timer(io_context),
        socket(io_context), outer(_outer), pool(graph->get_thread_pool())
        //, queue(io_context)
        ,
        turnstile(io_context), connecting_condition(_io_context) {
    // perfetto::Track t(io_track);
    // auto desc = t.Serialize();
    // desc.set_name("SpikeGLX I/O");
    // perfetto::TrackEvent::SetTrackDescriptor(t, desc);
    // queue.start();
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    (*state)["Running"].assign(false);
    memset(&address, 0, sizeof(address));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() { (*state)["Running"].assign(false); }

  // struct CoQueue {
  //   boost::asio::io_context& io_context;
  //   CoCondition in_condition;
  //   CoCondition out_condition;
  //   std::list<std::function<boost::asio::awaitable<void>()>> queue;
  //   bool running = false;
  //   bool self_delete = false;

  //  CoQueue(boost::asio::io_context& io_context) : io_context(io_context),
  //  in_condition(io_context), out_condition(io_context) {}

  //  boost::asio::awaitable<void> run() {
  //    while(running) {
  //      co_await in_condition.wait([&] { return !queue.empty() || !running;
  //      }); if(self_delete) {
  //        delete this;
  //        break;
  //      }
  //      if(!running) {
  //        break;
  //      }
  //      co_await queue.front()();
  //      queue.pop_front();
  //      out_condition.notify();
  //    }
  //  }

  //  void start() {
  //    running = true;
  //    boost::asio::co_spawn(io_context, run(), boost::asio::detached);
  //  }
  //  void stop() {
  //    running = false;
  //    in_condition.notify();
  //  }

  //  template<typename T>
  //  boost::asio::awaitable<T>
  //  execute(std::function<boost::asio::awaitable<T>()> work) {
  //    std::optional<T> result;
  //    auto f = [&,work]() -> boost::asio::awaitable<void> {
  //      result = co_await work();
  //    };
  //    queue.push_back(f);
  //    in_condition.notify();
  //    co_await out_condition.wait([&]() { return result.has_value(); });
  //    co_return *result;
  //  }

  //  boost::asio::awaitable<void>
  //  execute(std::function<boost::asio::awaitable<void>()> work) {
  //    bool done = false;
  //    auto f = [&, work]() -> boost::asio::awaitable<void> {
  //      co_await work();
  //      done = true;
  //    };
  //    queue.push_back(f);
  //    in_condition.notify();
  //    co_await out_condition.wait([&]() { return done; });
  //  }
  //};

  // struct CoQueueHolder {
  //   CoQueue* queue;
  //   CoQueueHolder(boost::asio::io_context& io_context) : queue(new
  //   CoQueue(io_context)) {} ~CoQueueHolder() {
  //     if(queue->running) {
  //       queue->running = true;
  //       queue->self_delete = true;
  //       queue->stop();
  //     } else {
  //       delete queue;
  //     }
  //   }

  //  void start() {
  //    queue->start();
  //  }
  //  void stop() {
  //    queue->stop();
  //  }

  //  template<typename T>
  //  boost::asio::awaitable<T>
  //  execute(std::function<boost::asio::awaitable<T>()> work) {
  //    return queue->execute(work);
  //  }

  //  boost::asio::awaitable<void>
  //  execute(std::function<boost::asio::awaitable<void>()> work) {
  //    return queue->execute(work);
  //  }
  //};

  // CoQueueHolder queue;
  struct CoTurnstile {
    CoCondition condition;
    size_t next = 0;
    size_t current = 0;
    CoTurnstile(boost::asio::io_context &_io_context)
        : condition(_io_context) {}

    struct Turn {
      CoTurnstile &turnstile;
      bool holds = true;
      Turn() = delete;
      Turn(Turn &) = delete;
      Turn(const Turn &) = delete;

      Turn(CoTurnstile &t) : turnstile(t) {}
      Turn(Turn &&t) : turnstile(t.turnstile) { t.holds = false; }
      ~Turn() {
        if (holds) {
          ++turnstile.current;
          turnstile.condition.notify();
        }
      }
    };

    boost::asio::awaitable<Turn> wait() {
      auto ticket = next++;
      co_await condition.wait([&] { return ticket == current; });
      co_return Turn(*this);
    }
  };

  CoTurnstile turnstile;

  boost::asio::awaitable<std::string> co_query(std::string command, boost::system::error_code& ec) {
    // TRACE_EVENT("thalamus", "SpikeGlxNode::co_query",
    // perfetto::Track(io_track));
    auto turn = turnstile.wait();

    auto command_nl = command + "\n";
    auto count = 0ull;
    {
      // TRACE_EVENT("thalamus", "boost::asio::async_write",
      // perfetto::Track(io_track));
      std::tie(ec, count) = co_await boost::asio::async_write(
          socket, boost::asio::buffer(command_nl.data(), command_nl.size()),
          boost::asio::as_tuple(boost::asio::use_awaitable));
      if(ec) {
        co_return "";
      }
    }

    std::string data;
    {
      // TRACE_EVENT("thalamus", "boost::asio::async_read_until",
      // perfetto::Track(io_track));
      std::tie(ec, count) = co_await boost::asio::async_read_until(
          socket, boost::asio::dynamic_buffer(data), "OK\n",
          boost::asio::as_tuple(boost::asio::use_awaitable));
      if(ec) {
        co_return "";
      }
    }

    auto end = data.find("\n");
    data.resize(end);
    co_return data;
  }

  boost::asio::awaitable<std::string> co_query(std::string command) {
    boost::system::error_code ec;
    auto result = co_await co_query(command, ec);
    if(ec) {
      throw boost::system::system_error(ec);
    }
    co_return result;
  }

  boost::asio::awaitable<void> co_command(std::string command, boost::system::error_code& ec) {
    // TRACE_EVENT("thalamus", "SpikeGlxNode::co_command",
    // perfetto::Track(io_track));
    auto turn = turnstile.wait();

    auto command_nl = command + "\n";

    // TRACE_EVENT("thalamus", "boost::asio::async_write",
    // perfetto::Track(io_track));
    auto count = 0ull;
    std::tie(ec, count) = co_await boost::asio::async_write(
        socket, boost::asio::buffer(command_nl.data(), command_nl.size()),
        boost::asio::as_tuple(boost::asio::use_awaitable));
  }

  boost::asio::awaitable<void> co_command(std::string command) {
    boost::system::error_code ec;
    co_await co_command(command, ec);
    if(ec) {
      throw boost::system::system_error(ec);
    }
  }

  void disconnect() {
    // TRACE_EVENT("thalamus", "SpikeGlxNode::disconnect",
    // perfetto::Track(io_track));
    if (!connected) {
      return;
    }
    boost::system::error_code ec;
    ec = socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    if (ec) {
      THALAMUS_LOG(error) << ec.what();
      (*state)["Error"].assign(ec.what());
      connected = false;
      return;
    }

    ec = socket.close(ec);
    if (ec) {
      THALAMUS_LOG(error) << ec.what();
      (*state)["Error"].assign(ec.what());
    }
    connected = false;
  }

  bool connected = false;
  bool connecting = false;
  CoCondition connecting_condition;

  boost::asio::awaitable<void> connect(boost::system::error_code& ec) {
    // TRACE_EVENT("thalamus", "SpikeGlxNode::connect",
    // perfetto::Track(io_track));
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
      if (!state->contains("Address")) {
        throw std::runtime_error("No Address defined");
      }

      std::string address_str = state->at("Address");
      std::vector<std::string> address_tokens =
          absl::StrSplit(address_str, ':');
      if (address_tokens.size() == 1) {
        address_tokens.push_back("4142");
      }

      if (address_tokens.size() != 2) {
        throw std::runtime_error(std::string("Failed to parse address :") +
                                 address_str);
      }

      boost::asio::ip::tcp::resolver resolver(io_context);
      decltype(resolver.async_resolve(
          address_tokens.at(0), address_tokens.at(1),
          boost::asio::use_awaitable))::value_type endpoints;
      {
        // TRACE_EVENT("thalamus",
        // "boost::asio::ip::tcp::resolver::async_resolve",
        // perfetto::Track(io_track));
        std::tie(ec, endpoints) = co_await resolver.async_resolve(address_tokens.at(0),
                                                    address_tokens.at(1),
                                                    boost::asio::as_tuple(boost::asio::use_awaitable));
        if(ec) {
          co_return;
        }
      }

      decltype(boost::asio::async_connect(socket, endpoints, boost::asio::use_awaitable))::value_type connect_value;
      {
        // TRACE_EVENT("thalamus", "boost::asio::async_connect",
        // perfetto::Track(io_track));
        std::tie(ec, connect_value) = co_await boost::asio::async_connect(socket, endpoints,
                                            boost::asio::as_tuple(boost::asio::use_awaitable));
        if(ec) {
          co_return;
        }
      }

      auto text = co_await co_query("GETVERSION", ec);
      if(ec) {
        co_return;
      }
      std::vector<std::string_view> tokens =
          absl::StrSplit(text, absl::ByAnyChar(".,"));
      if (tokens.size() < 2) {
        throw std::runtime_error(
            std::string("Failed to parse SpikeGLX version: ") + text);
      }

      auto success = absl::SimpleAtoi(tokens[1], &spike_glx_version);
      if (!success) {
        throw std::runtime_error(
            std::string("Failed to parse SpikeGLX version :") + text);
      }

      auto command =
          spike_glx_version < 20240000 ? "GETIMPROBECOUNT" : "GETSTREAMNP 2";
      text = co_await co_query(command, ec);
      if(ec) {
        co_return;
      }
      imec_count = parse_number<long long>(text);
      imec_data.resize(size_t(imec_count));
      imec_names.resize(size_t(imec_count));

      int imec_pending = 1;
      CoCondition condition(io_context);
      auto tracker = [&]() {
        --imec_pending;
        condition.notify();
      };
      (*state)["imec_count"].assign(imec_count, tracker);

      for (auto i = 0ll; i < imec_count; ++i) {
        auto subset_text = absl::StrFormat("imec_subset_%d", i);
        if (state->contains(subset_text)) {
          continue;
        }

        ++imec_pending;
        (*state)[subset_text].assign("*", tracker);
      }

      // std::cout << "imec wait" << std::endl;
      co_await condition.wait([&] { return imec_pending == 0; });
      // std::cout << "imec waited" << std::endl;
      connected = true;
      (*state)["Connected"].assign(true);
    } catch (std::exception &e) {
      THALAMUS_LOG(error) << boost::diagnostic_information(e);
      (*state)["Error"].assign(e.what());
      (*state)["Connected"].assign(false);
      disconnect();
      co_return;
    }
  }

  boost::asio::awaitable<void> connect() {
    boost::system::error_code ec;
    co_await connect(ec);
    if(ec) {
      (*state)["Connected"].assign(false);
    }
  }

  long long imec_count = 0;

  std::vector<std::string> imec_subsets;
  bool streaming = false;
  bool do_stream = false;

  size_t spike_glx_version;

  std::string scan_count_command(Device js, int ip) {
    if (js == Device::IMEC) {
      if (spike_glx_version < 20240000) {
        return absl::StrFormat("GETSCANCOUNT %d", ip);
      } else {
        return absl::StrFormat("GETSTREAMSAMPLECOUNT 2 %d", ip);
      }
    } else {
      if (spike_glx_version < 20240000) {
        return "GETSCANCOUNT -1";
      } else {
        return "GETSTREAMSAMPLECOUNT 0 0";
      }
    }
  }

  std::string sample_rate_command(Device js, int ip) {
    if (js == Device::IMEC) {
      if (spike_glx_version < 20240000) {
        return absl::StrFormat("GETSAMPLERATE %d", ip);
      } else {
        return absl::StrFormat("GETSTREAMSAMPLERATE 2 %d", ip);
      }
    } else {
      if (spike_glx_version < 20240000) {
        return "GETSAMPLERATE -1";
      } else {
        return "GETSTREAMSAMPLERATE 0 0";
      }
    }
  }

  std::string fetch_command(Device js, int ip, size_t offset,
                            const std::string &subset) {
    if (js == Device::IMEC) {
      if (spike_glx_version < 20240000) {
        return absl::StrFormat("FETCH %d %d 50000 %s", ip, offset, subset);
      } else {
        return absl::StrFormat("FETCH 2 %d %d 50000 %s", ip, offset, subset);
      }
    } else {
      if (spike_glx_version < 20240000) {
        return absl::StrFormat("FETCH -1 %d 50000 %s", offset, subset);
      } else {
        return absl::StrFormat("FETCH 0 0 %d 50000 %s", offset, subset);
      }
    }
  }

  boost::asio::awaitable<void> send_metadata(boost::system::error_code& ec) {
    if(!new_metadata.empty()) {
      co_await co_command("SETMETADATA", ec);
      if(ec) {
        co_return;
      }
      for(auto& pair : new_metadata) {
        std::string key = pair->at("Key");
        ObservableCollection::Value value = pair->at("Value");
        std::stringstream stream;
        std::visit([&](const auto& arg) {
            if constexpr (std::is_same<decltype(arg), std::string>::value
                || std::is_same<decltype(arg), double>::value
                || std::is_same<decltype(arg), long long>::value) {
              stream << std::get<std::string>(key) << "=" << arg;
            }
        }, value);
        if(!stream.str().empty()) {
          co_await co_command(stream.str());
          if(ec) {
            co_return;
          }
        }
      }
      new_metadata.clear();
      co_await co_query("");
      if(ec) {
        co_return;
      }
    }
  }

  boost::asio::awaitable<void> stream() {
    boost::system::error_code ec;

    auto check_error = [&] {
      if (ec) {
        THALAMUS_LOG(error) << ec.what();
        (*state)["Error"].assign(ec.what());
        (*state)["Connected"].assign(false);
        (*state)["Running"].assign(false);
        return true;
      }
      return false;
    };

    try {
      co_await connect(ec);
      if (!connected) {
        co_return;
      }

      if (streaming) {
        co_return;
      }
      streaming = true;
      Finally f([&] { streaming = false; });

      std::map<std::pair<Device, int>, size_t> sample_counts;
      sample_intervals.clear();
      std::vector<std::pair<Device, int>> inputs;
      for (auto i = 0; i < imec_count; ++i) {
        inputs.emplace_back(Device::IMEC, i);
      }
      inputs.emplace_back(Device::NI, 0);

      for (auto &pair : inputs) {
        auto [js, ip] = pair;
        auto text = co_await co_query(scan_count_command(js, ip), ec);
        if(check_error()) {
          co_return;
        }
        THALAMUS_LOG(info) << "scan_count_command " << text;

        size_t count;
        auto success = absl::SimpleAtoi(text, &count);
        sample_counts[std::make_pair(js, ip)] = count;
        if (!success) {
          throw std::runtime_error(
              std::string("Failed to parse sample count: ") + text);
        }

        text = co_await co_query(sample_rate_command(js, ip), ec);
        if(check_error()) {
          co_return;
        }
        THALAMUS_LOG(info) << "sample_rate_command " << text;

        double rate;
        success = absl::SimpleAtod(text, &rate);
        sample_intervals[std::make_pair(js, ip)] =
            std::chrono::nanoseconds(size_t(1000000000 / rate));
        if (!success) {
          throw std::runtime_error(
              std::string("Failed to parse sample rate: ") + text);
        }
      }

      size_t offset = 0;
      size_t fill = 0;
      char *char_buffer = reinterpret_cast<char *>(buffer);
      size_t total_samples = 0;

      auto do_read = [&]() -> boost::asio::awaitable<void> {
        // std::cout << "do_read " << offset << " " << fill << std::endl;
        if (fill == sizeof(buffer)) {
          std::copy(buffer + offset, buffer + fill, buffer);
          fill -= offset;
          offset = 0;
        }
        // TRACE_EVENT("thalamus", "SpikeGlxNode::do_read");
        size_t count;
        std::tie(ec, count) = co_await socket.async_receive(
            boost::asio::buffer(buffer + fill, sizeof(buffer) - fill),
            boost::asio::as_tuple(boost::asio::use_awaitable));
        // std::cout << "do_read " << offset << " " << fill << " " << count <<
        // std::endl;
        fill += count;
      };

      co_await co_query("SETRECORDENAB 1", ec);
      if(check_error()) {
        co_return;
      }

      boost::asio::steady_timer poll_timer(io_context);

      if(!do_stream) {
        while(streaming) {
          co_await send_metadata(ec);
          if(check_error()) {
            co_return;
          }
          poll_timer.expires_after(1s);
          co_await poll_timer.async_wait();
        }
        co_return;
      }
      while (streaming) {
        if(!new_metadata.empty()) {
          co_await send_metadata(ec);
          if(check_error()) {
            co_return;
          }
        }

        auto start_time = std::chrono::steady_clock::now();
        for (auto &pair : inputs) {
          auto [js, ip] = pair;
          auto j = sample_counts.find(pair);
          auto subset = js == Device::IMEC ? imec_subsets[size_t(ip)] : "";
          auto command = fetch_command(js, ip, j->second, subset);
          co_await co_command(command, ec);
          if(check_error()) {
            co_return;
          }
        }

        for (auto &pair : inputs) {
          auto [js, ip] = pair;

          auto &data = js == Device::IMEC ? imec_data[size_t(ip)] : ni_data;
          auto &names = js == Device::IMEC ? imec_names[size_t(ip)] : ni_names;
          auto &sample_count = sample_counts[std::make_pair(js, ip)];
          auto sample_interval = sample_intervals[std::make_pair(js, ip)];
          auto prefix =
              (js == Device::IMEC ? "IMEC:" : "NI:") + std::to_string(ip) + ":";
          while (true) {
            if (offset == fill) {
              co_await do_read();
              if(check_error()) {
                co_return;
              }
            }
            auto no_data = false;
            auto found_header = false;
            for (auto i = offset; i < fill; ++i) {
              if (char_buffer[i] == '\n') {
                // std::cout << "HEADER" << std::endl;
                TRACE_EVENT("thalamus",
                            "SpikeGlxNode::stream Read BINARY_DATA header");
                char_buffer[i] = 0;
                found_header = true;
                if (std::string_view(char_buffer + offset, i)
                        .starts_with("ERROR FETCH: No data")) {
                  for (auto &d : data) {
                    d.clear();
                  }
                  no_data = true;
                  total_samples = 0;
                  offset = i + 1;
                  break;
                }

                auto scan_count = sscanf(char_buffer + offset,
                                         "BINARY_DATA %d %d uint64(%llu)",
                                         &nchans, &nsamples, &from_count);
                // std::cout << (char_buffer + offset) << " " << nchans << " "
                // << nsamples << " " << from_count << std::endl;
                if (scan_count < 3) {
                  throw std::runtime_error(
                      std::string("Failed to read BINARY_DATA header: ") +
                      (char_buffer + offset));
                }
                data.resize(size_t(nchans));
                for (auto &d : data) {
                  d.clear();
                }
                names.resize(size_t(nchans));
                for (size_t j = 0; j < names.size(); ++j) {
                  auto &name = names[j];
                  if (name.empty()) {
                    name = prefix + std::to_string(j);
                  }
                }
                samples_read = 0;
                next_channel = 0;
                complete_samples = 0;
                total_samples = size_t(nchans * nsamples);

                offset = i + 1;
                break;
              }
            }
            if (!found_header) {
              co_await do_read();
              if(check_error()) {
                co_return;
              }
              continue;
            }
            if (no_data) {
              break;
            }

            auto band_size = std::max(1u, uint32_t(nchans) / pool.num_threads);
            band_size += (uint32_t(nchans) % band_size) ? 1 : 0;
            int total_bands = nchans / int(band_size);
            total_bands += (uint32_t(nchans) % band_size) ? 1 : 0;
            std::mutex mutex;
            std::condition_variable cond;

            while (samples_read < total_samples) {
              if (fill - offset < 2) {
                co_await do_read();
                if(check_error()) {
                  co_return;
                }
              }
              while (fill - offset < 2 * (total_samples - samples_read) &&
                     fill < sizeof(buffer)) {
                // TRACE_EVENT("thalamus", "SpikeGlxNode::stream read more");
                co_await do_read();
                if(check_error()) {
                  co_return;
                }
              }

              auto data_end =
                  std::min(offset + 2 * (total_samples - samples_read), fill);
              {
                TRACE_EVENT("thalamus", "SpikeGlxNode::stream read all bands");
                int pending_bands = total_bands;
                for (auto c = 0; c < nchans; c += band_size) {
                  pool.push([&, c] {
                    TRACE_EVENT("thalamus",
                                "SpikeGlxNode::stream read single band");
                    for (auto subc = 0;
                         subc < int(band_size) && c + subc < nchans; ++subc) {
                      auto channel =
                          (samples_read + size_t(c + subc)) % size_t(nchans);
                      for (auto i = offset + size_t(2 * (c + subc));
                           i + 1 < data_end; i += size_t(2 * nchans)) {
                        short sample = short(buffer[i] + (buffer[i + 1] << 8));
                        data[channel].push_back(sample);
                      }
                    }
                    {
                      std::lock_guard<std::mutex> lock(mutex);
                      --pending_bands;
                      cond.notify_all();
                    }
                  });
                }
                std::unique_lock<std::mutex> lock(mutex);
                cond.wait(lock, [&] { return pending_bands == 0; });
              }

              samples_read += (data_end - offset) / 2;
              offset += 2 * ((data_end - offset) / 2);
              // std::cout << samples_read << " " << total_samples << " " <<
              // offset << " " << std::endl;
            }
            // std::cout << "Publish" << std::endl;

            auto now = std::chrono::steady_clock::now();
            time = now.time_since_epoch();
            latency =
                short(std::chrono::duration_cast<std::chrono::milliseconds>(
                          nsamples * sample_interval)
                          .count());
            complete_samples = std::numeric_limits<size_t>::max();
            for (auto &d : data) {
              complete_samples = std::min(complete_samples, d.size());
            }
            auto last_num_channels = num_channels;
            num_channels = ni_data.size();
            for (auto &d : imec_data) {
              num_channels += d.size();
            }
            if (num_channels != last_num_channels) {
              // std::cout << "channels_changed" << std::endl;
              TRACE_EVENT("thalamus", "SpikeGlxNode::channels_changed");
              outer->channels_changed(outer);
            }
            if (complete_samples > 0) {
              current_js = js;
              current_ip = ip;
              // std::cout << "ready" << std::endl;
              TRACE_EVENT("thalamus", "SpikeGlxNode::ready");
              outer->ready(outer);
            }
            // std::cout << "fetch done" << std::endl;
            // THALAMUS_LOG(info) << "fetch done";
            sample_count += uint64_t(nsamples);

            auto found_ok = false;
            while (!found_ok) {
              while (offset + 3 <= fill) {
                found_ok = std::string_view(char_buffer + offset, 3) == "OK\n";
                if (found_ok) {
                  // std::cout << "OK" << std::endl;
                  offset += 3;
                  break;
                }
                ++offset;
              }
              if (!found_ok) {
                co_await do_read();
                if(check_error()) {
                  co_return;
                }
              }
            }
            break;
          }
        }

        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = end_time - start_time;
        if (elapsed < poll_interval) {
          poll_timer.expires_after(poll_interval - elapsed);
          co_await poll_timer.async_wait();
        }
      }
    } catch (std::exception &e) {
      THALAMUS_LOG(error) << boost::diagnostic_information(e);
      check_error();
      co_return;
    }
  }

  boost::asio::awaitable<void> stop_stream() {
    TRACE_EVENT("thalamus", "SpikeGlxNode::stop_stream");
    streaming = false;
    if(connected) {
      boost::system::error_code ec;
      co_await co_query("SETRECORDENAB 0", ec);
    }
  }

  std::set<ObservableDictPtr> new_metadata;

  void on_metadata_change(ObservableCollection* source,
                 ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    if(source == metadata_node.get()) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Metadata") {
        metadata_list = std::get<ObservableListPtr>(v);
        metadata_list->recap(std::bind(&Impl::on_metadata_change, this, metadata_list.get(), _1, _2, _3));
      }
    } else if(source == metadata_list.get()) {
      new_metadata.insert(std::get<ObservableDictPtr>(v));
    }
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "SpikeGlxNode::on_change");
    auto key_str = std::get<std::string>(k);
    if (key_str.starts_with("imec_subset")) {
      std::vector<std::string> tokens = absl::StrSplit(key_str, '_');
      auto index = parse_number<size_t>(tokens.back());
      imec_subsets.resize(index + 1, "*");
      imec_subsets[index] = std::get<std::string>(v);
    } else if (key_str == "Metadata Source") {
      auto nodes = dynamic_cast<ObservableList*>(state->parent);
      auto value_str = std::get<std::string>(v);
      for(auto& node : *nodes) {
        ObservableDictPtr dict = node;
        std::string name = dict->at("name");
        if(name == value_str) {
          metadata_node = dict;
          metadata_connection = dict->recursive_changed.connect(std::bind(&Impl::on_metadata_change, this, _1, _2, _3, _4));
          dict->recap(std::bind(&Impl::on_metadata_change, this, metadata_node.get(), _1, _2, _3));
        }
      }
    } else if (key_str == "Connected") {
      auto new_is_connected = std::get<bool>(v);
      if (new_is_connected) {
        boost::asio::co_spawn(io_context, connect(), boost::asio::detached);
      } else {
        disconnect();
      }
    } else if (key_str == "Stream") {
      do_stream = std::get<bool>(v);
    } else if (key_str == "Running") {
      auto is_running = std::get<bool>(v);
      if (is_running) {
        boost::asio::co_spawn(io_context, stream(), boost::asio::detached);
      } else {
        boost::asio::co_spawn(io_context, stop_stream(), boost::asio::detached);
      }
    } else if (key_str == "Poll Interval (ms)") {
      poll_interval = std::chrono::milliseconds(std::get<long long>(v));
    }
  }

  std::chrono::milliseconds poll_interval = 10ms;
};

SpikeGlxNode::SpikeGlxNode(ObservableDictPtr state,
                           boost::asio::io_context &io_context,
                           NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

SpikeGlxNode::~SpikeGlxNode() {}

std::span<const double> SpikeGlxNode::data(int) const {
  THALAMUS_ASSERT(false, "SpikeGlxNode publishes short data");
  return std::span<const double>();
}

std::span<const short> SpikeGlxNode::short_data(int channel) const {
  if (channel == 0) {
    return std::span<const short>(&impl->latency, &impl->latency + 1);
  }
  --channel;

  for (auto j = 0ull; j < impl->imec_data.size(); ++j) {
    auto &channels = impl->imec_data[j];
    if (size_t(channel) < channels.size()) {
      if (impl->current_js == Impl::Device::IMEC &&
          size_t(impl->current_ip) == j) {
        return std::span<const short>(channels[size_t(channel)].begin(),
                                      channels[size_t(channel)].begin() +
                                          int64_t(impl->complete_samples));
      } else {
        return std::span<const short>();
      }
    }
    channel -= channels.size();
  }

  if (size_t(channel) < impl->ni_data.size() &&
      impl->current_js == Impl::Device::NI) {
    return std::span<const short>(impl->ni_data[size_t(channel)].begin(),
                                  impl->ni_data[size_t(channel)].begin() +
                                      int64_t(impl->complete_samples));
  }

  return std::span<const short>();
}

std::string_view SpikeGlxNode::name(int channel) const {
  if (channel == 0) {
    return "Latency (ms)";
  }
  --channel;

  for (const auto &names : impl->imec_names) {
    if (size_t(channel) < names.size()) {
      return names[size_t(channel)];
    }
    channel -= names.size();
  }

  if (size_t(channel) < impl->ni_data.size()) {
    return impl->ni_names[size_t(channel)];
  }

  return "";
}

int SpikeGlxNode::num_channels() const { return int(impl->num_channels + 1); }

std::chrono::nanoseconds SpikeGlxNode::sample_interval(int i) const {
  if (i == 0) {
    return 0ns;
  }
  --i;

  for (auto j = 0ull; j < impl->imec_data.size(); ++j) {
    auto &channels = impl->imec_data[j];
    if (size_t(i) < channels.size()) {
      return impl->sample_intervals[std::make_pair(Impl::Device::IMEC, int(j))];
    }
    i -= channels.size();
  }

  if (size_t(i) < impl->ni_data.size()) {
    return impl->sample_intervals[std::make_pair(Impl::Device::NI, 0)];
  }

  return 0ns;
}

std::chrono::nanoseconds SpikeGlxNode::time() const { return impl->time; }

std::string SpikeGlxNode::type_name() { return "SPIKEGLX"; }

void SpikeGlxNode::inject(const thalamus::vector<std::span<double const>> &,
                          const thalamus::vector<std::chrono::nanoseconds> &,
                          const thalamus::vector<std::string_view> &) {}

size_t SpikeGlxNode::modalities() const { return THALAMUS_MODALITY_ANALOG; }

bool SpikeGlxNode::is_short_data() const { return true; }

boost::json::value SpikeGlxNode::process(const boost::json::value &) {
  boost::asio::co_spawn(impl->io_context, impl->connect(),
                        boost::asio::detached);
  return boost::json::value();
  // impl->connect([&] {
  //   auto command = impl->spike_glx_version < 20240000 ? "GETIMPROBECOUNT" :
  //   "GETSTREAMNP"; impl->query_string(command, [&](const auto& text) {
  //     auto count = parse_number<int>(text);
  //     impl->imec_data.resize(count);
  //     (*impl->state)["imec_count"].assign(count);
  //     for(auto i = 0;i < count:++i) {
  //       std::string command;
  //       if(impl->spike_glx_version < 20240000) {
  //         command = absl::StrFormat("GETACQCHANCOUNTS %d", i);
  //       } else {
  //         command = absl::StrFormat("GETSTREAMACQCHANS 2 %d", i);
  //       }
  //       impl->query_string(command, [&,i](const auto& text) {
  //         auto tokens = absl::StrSplit(text, ' ');
  //         auto sum = 0;
  //         for(auto token : tokens) {
  //           sum += parse_number<int>(token);
  //         }
  //         impl->imec_data[i].resize(sum);
  //       })
  //     }
  //   });
  // });
}

// unsigned long long SpikeGlxNode::Impl::io_track = get_unique_id();
