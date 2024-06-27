#include "libs/asio/include/boost/asio/io_context.hpp"
#include <nidaq_node.h>
#include <regex>
#include <absl/strings/numbers.h>
#include <tracing/tracing.h>
#include <numeric>
#include <grpc_impl.h>
#include <comedilib.h>

namespace thalamus {
  static void close_device(comedi_t** device) {
    if(*device != nullptr) {
      comedi_close(*device);
      *device = nullptr;
    }
  }

  struct DeviceParams {
    unsigned int device;
    unsigned int subdevice;
    thalamus::vector<unsigned int> channels;
  };

  bool is_digital(const std::string& channel) {
    return channel.find("port") != std::string::npos;
  }

  static std::regex nidaq_regex("([a-zA-Z0-9/])+(\\d+)(:(\\d+))?$");

  static thalamus::vector<std::string> get_channels(const std::string& channel) {
    std::smatch match_result;
    if (!std::regex_search(channel, match_result, nidaq_regex)) {
      //QMessageBox::warning(nullptr, "Parse Failed", "Failed to parse NIDAQ channel");
      return thalamus::vector<std::string>();
    }
    if (!match_result[4].matched) {
      return thalamus::vector<std::string>(1, channel);
    }
    else {
      auto base_str = match_result[1].str();
      auto left_str = match_result[2].str();
      auto right_str = match_result[4].str();
      int left, right;
      auto success = absl::SimpleAtoi(left_str, &left);
      THALAMUS_ASSERT(success);
      success = absl::SimpleAtoi(right_str, &right);
      THALAMUS_ASSERT(success);
      thalamus::vector<std::string> result;
      for(auto i = left;i <= right;++i) {
        result.push_back(base_str + std::to_string(i));
      }
      return result;
    }
  }

  static std::optional<DeviceParams> parse_device_string(const std::string& device) {
    std::regex regex("[a-zA-Z]+(\\d+)/([a-zA-Z]+)(\\d+)(:(\\d+))?(/[a-zA-Z]+(\\d+)(:(\\d+))?)?$");
    std::smatch match_result;
    if (!std::regex_search(device, match_result, regex)) {
      //QMessageBox::warning(nullptr, "Parse Failed", "Failed to parse NIDAQ channel");
      return std::nullopt;
    }
    DeviceParams result;
    absl::SimpleAtoi(match_result[1].str(), &result.device);

    if(match_result[6].matched) {
      absl::SimpleAtoi(match_result[3].str(), &result.subdevice);
      auto left_str = match_result[7].str();
      auto right_str = match_result[8].matched ? match_result[9].str() : left_str;
      int left, right;
      absl::SimpleAtoi(left_str, &left);
      absl::SimpleAtoi(right_str, &right);
      for(auto i = left;i < right+1;++i) {
        result.channels.push_back(i);
      }
    } else {
      auto left_str = match_result[3].str();
      auto right_str = match_result[4].matched ? match_result[5].str() : left_str;
      int left, right;
      absl::SimpleAtoi(left_str, &left);
      absl::SimpleAtoi(right_str, &right);
      for(auto i = left;i < right+1;++i) {
        result.channels.push_back(i);
      }
    }

    return result;
  }

  struct NidaqNode::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    comedi_t* device;
    unsigned int subdevice;
    thalamus::vector<unsigned int> channels;
    boost::asio::io_context& io_context;
    boost::asio::high_resolution_timer timer;
    size_t analog_buffer_position;
    thalamus::vector<std::vector<double>> analog_buffer;
    thalamus::vector<double> output_buffer;
    thalamus::vector<std::span<double const>> spans;
    size_t _num_channels;
    size_t buffer_size;
    std::chrono::nanoseconds _sample_interval;
    std::chrono::milliseconds polling_interval;
    size_t counter = 0;
    std::chrono::nanoseconds _time = 0ns;
    std::atomic_bool busy;
    std::thread nidaq_thread;
    int _every_n_samples;
    std::list<thalamus::vector<double>> buffers;
    NodeGraph* graph;
    NidaqNode* outer;
    std::atomic_bool is_running;
    bool digital;
    thalamus::vector<std::string> recommended_names;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, NidaqNode* outer)
      : state(state)
      , device(nullptr)
      , io_context(io_context)
      , timer(io_context)
      , busy(false)
      , analog_buffer_position(0)
      , graph(graph)
      , outer(outer)
      , is_running(false) {
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
      boost::asio::io_context q();
    }

    ~Impl() {
      (*state)["Running"].assign(false, [] {});
      close_device(&device);
    }

    void nidaq_target() {
      thalamus::vector<comedi_range*> ranges;
      if(!digital) {
        for(auto c : channels) {
          auto range = comedi_get_range(device, subdevice, c, 0);
          ranges.push_back(range);
        }
      }
      while (is_running) {
        auto start = std::chrono::steady_clock::now();
        auto now = start;
        auto target_time = start;
        if(digital) {
          std::vector<std::vector<double>> buffer(channels.size());
          while(is_running) {
            for(auto c = 0;c < channels.size();++c) {
              unsigned int bit;
              comedi_dio_read(device, subdevice, channels.at(c), &bit);
              buffer.at(c).push_back(bit ? 5 : 0);
            }
            target_time += _sample_interval;
            while(target_time <= now) {
              for(auto c = 0;c < channels.size();++c) {
                buffer.at(c).push_back(buffer.at(c).back());
              }
              target_time += _sample_interval;
            }
            std::this_thread::sleep_until(target_time);
            now = std::chrono::steady_clock::now();
            if(now - start >= polling_interval) {
              boost::asio::post(io_context, [this,buffer=std::move(buffer),start]() {
                TRACE_EVENT0("thalamus", "NidaqCallback(post)");
                auto impl = this;
                impl->output_buffer.clear();
                impl->spans.clear();
                //std::cout << impl->_sample_interval.count() << " " << impl->polling_interval.count() << " ";
                for(auto b : buffer) {
                  //std::cout << b.size() << " ";
                  impl->output_buffer.insert(impl->output_buffer.end(), b.begin(), b.end());
                }
                //std::cout << std::endl;
                auto offset = 0;
                for(auto b : buffer) {
                  impl->spans.emplace_back(impl->output_buffer.begin() + offset, impl->output_buffer.begin() + offset + b.size());
                  offset += b.size();
                }

                impl->_time += start.time_since_epoch();
                outer->ready(outer);
              });
              start = now;
              buffer = std::vector<std::vector<double>>(channels.size());
            }
          }
        } else {
          std::vector<std::vector<double>> buffer(channels.size());
          while(is_running) {
            for(auto c = 0;c < channels.size();++c) {
              unsigned int bit;
              comedi_dio_read(device, subdevice, channels.at(c), &bit);
              buffer.at(c).push_back(bit ? 5 : 0);
            }
            target_time += _sample_interval;
            std::this_thread::sleep_until(target_time);
            now = std::chrono::steady_clock::now();
            if(now - start >= polling_interval) {
              boost::asio::post(io_context, [this,buffer=std::move(buffer),start]() {
                TRACE_EVENT0("thalamus", "NidaqCallback(post)");
                auto impl = this;
                impl->output_buffer.clear();
                impl->spans.clear();
                for(auto b : buffer) {
                  std::cout << b.size() << " ";
                  impl->output_buffer.insert(impl->output_buffer.end(), b.begin(), b.end());
                }
                std::cout << std::endl;
                auto offset = 0;
                for(auto b : buffer) {
                  impl->spans.emplace_back(impl->output_buffer.begin() + offset, impl->output_buffer.begin() + offset + b.size());
                  offset += b.size();
                }

                impl->_time += start.time_since_epoch();
                outer->ready(outer);
              });
              start = now;
              buffer = std::vector<std::vector<double>>(channels.size());
            }
          }
        }
      }
    }

    void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Channel") {
        std::string channel = state->at("Channel");
        recommended_names = get_channels(channel);
        _num_channels = recommended_names.size();
      }
      else if (key_str == "Running") {
        is_running = std::get<bool>(v);
        if (is_running) {
          counter = 0;
          std::string name = state->at("name");
          std::string channel = state->at("Channel");
          double sample_rate = state->at("Sample Rate");

          auto device_params = parse_device_string(channel);
          if(!device_params) {
            (*state)["Running"].assign(false, [&] {});
            return;
          }
          subdevice = device_params->subdevice;
          channels = device_params->channels;

          _sample_interval = std::chrono::nanoseconds(size_t(1e9 / sample_rate));

          size_t polling_interval_raw = state->at("Poll Interval");
          polling_interval = std::chrono::milliseconds(polling_interval_raw);

          std::string channel_name = name + " channel";
          buffer_size = static_cast<size_t>(sample_rate * _num_channels);
          std::function<void()> reader;

          digital = is_digital(channel);
          if(!digital) {
            subdevice = comedi_find_subdevice_by_type(device, COMEDI_SUBD_AI, 0);
          }

          auto filename = absl::StrFormat("/dev/comedi%d", device_params->device-1);
          device = comedi_open(filename.c_str());
          BOOST_ASSERT_MSG(device != nullptr, "comedi_open failed");

          if (digital) {
            for(auto channel : channels) {
              auto daq_error = comedi_dio_config(device, subdevice, channel, COMEDI_INPUT);
              BOOST_ASSERT_MSG(daq_error >= 0, "comedi_dio_config failed");
            }
          }
          _time = 0ns;
          nidaq_thread = std::thread(std::bind(&Impl::nidaq_target, this));

          //if (reader) {
          //  on_timer(reader, polling_interval, boost::system::error_code());
          //}
        }
        else if (device != nullptr) {
          if (nidaq_thread.joinable()) {
            nidaq_thread.join();
          }
          close_device(&device);
        }
      }
    }
  };

  NidaqNode::NidaqNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph) : impl(new Impl(state, io_context, graph, this)) {}

  NidaqNode::~NidaqNode() {}

  int NidaqNode::get_num_channels(const std::string& channel) {
    std::regex regex("[a-zA-Z0-9/]+(\\d+)(:(\\d+))?$");
    std::smatch match_result;
    if (!std::regex_search(channel, match_result, regex)) {
      //QMessageBox::warning(nullptr, "Parse Failed", "Failed to parse NIDAQ channel");
      return -1;
    }
    if (!match_result[2].matched) {
      return 1;
    }
    else {
      auto left_str = match_result[1].str();
      auto right_str = match_result[3].str();
      int left, right;
      absl::SimpleAtoi(left_str, &left);
      absl::SimpleAtoi(right_str, &right);
      return right - left + 1;
    }
  }

  std::string NidaqNode::type_name() {
    return "NIDAQ (COMEDI)";
  }

  std::span<const double> NidaqNode::data(int channel) const {
    return impl->spans.at(channel);
  }

  int NidaqNode::num_channels() const {
    return impl->_num_channels;
  }

  std::chrono::nanoseconds NidaqNode::sample_interval(int i) const {
    return impl->_sample_interval;
  }

  std::chrono::nanoseconds NidaqNode::time() const {
    return impl->_time;
  }

  std::string_view NidaqNode::name(int channel) const {
    return impl->recommended_names.at(channel);
  }
  std::span<const std::string> NidaqNode::get_recommended_channels() const {
    return std::span<const std::string>(impl->recommended_names.begin(), impl->recommended_names.end());
  }

  void NidaqNode::inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>&) {
    auto temp = impl->_num_channels;
    auto previous_sample_interval = impl->_sample_interval;
    impl->_num_channels = spans.size();
    impl->spans = spans;
    impl->_sample_interval = sample_intervals.at(0);
    ready(this);
    impl->_sample_interval = previous_sample_interval;
    impl->_num_channels = temp;
  }

  struct NidaqOutputNode::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    comedi_t* device;
    unsigned int subdevice;
    std::vector<unsigned int> channels;
    NodeGraph* graph;
    std::weak_ptr<Node> source;
    boost::asio::io_context& io_context;
    std::thread nidaq_thread;
    std::mutex mutex;
    std::condition_variable condition_variable;
    std::vector<std::vector<double>> buffers;
    boost::asio::high_resolution_timer timer;
    std::vector<std::span<const double>> _data;
    size_t _num_channels;
    size_t buffer_size;
    boost::signals2::scoped_connection source_connection;
    std::map<size_t, std::function<void(Node*)>> observers;
    double _sample_rate;
    thalamus::vector<std::chrono::nanoseconds> _sample_intervals;
    size_t counter = 0;
    std::chrono::nanoseconds _time;
    NidaqOutputNode* outer;
    bool running;
    std::vector<std::chrono::steady_clock::time_point> next_write;
    std::vector<std::chrono::steady_clock::time_point> times;
    bool digital = false;
    std::vector<bool> digital_levels;
    std::atomic_bool new_buffers = false;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, NidaqOutputNode* outer)
      : state(state)
      , device(nullptr)
      , io_context(io_context)
      , timer(io_context)
      , graph(graph)
      , outer(outer)
      , running(false) {
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [&] {});
      close_device(&device);
    }

    void nidaq_target() {
      while (running) {
        TRACE_EVENT0("thalamus", "NidaqOutputNode::on_data");
        std::vector<std::vector<double>> buffers;
        std::vector<std::chrono::nanoseconds> sample_intervals;
        {
          std::unique_lock<std::mutex> lock(mutex);
          condition_variable.wait(lock, [&] {
            return !running || !this->buffers.empty();
          });
          if (!running) {
            continue;
          }
          buffers.swap(this->buffers);
          sample_intervals.swap(_sample_intervals);
          new_buffers = false;
        }
        std::vector<size_t> positions(buffers.size(), 0);
        times.resize(buffers.size(), std::chrono::steady_clock::now());
        digital_levels.resize(buffers.size(), false);

        auto is_done = [&] {
          auto result = true;
          for (auto i = 0; i < buffers.size(); ++i) {
            result = result && positions.at(i) == buffers.at(i).size();
          }
          return result;
        };

        if (digital) {
          while (!is_done()) {
            {
              //TRACE_EVENT0("thalamus", "NidaqOutputNode::fast_forward(digital)");
              for (auto c = 0; c < buffers.size(); ++c) {
                auto& buffer = buffers.at(c);
                auto& position = positions.at(c);
                auto old_level = digital_levels.at(c);
                auto& time = times.at(c);

                auto new_level = position < buffer.size() ? buffer.at(position) > 1.6 : false;
                while (position < buffer.size() && new_level == old_level) {
                  ++position;
                  new_level = position < buffer.size() ? buffer.at(position) > 1.6 : false;
                  time += sample_intervals.at(c);
                }
              }
            }

            if (is_done()) {
              break;
            }

            auto next_time = std::min_element(times.begin(), times.end());
            auto now = std::chrono::steady_clock::now();
            {
              std::unique_lock<std::mutex> lock(mutex);
              auto wait_result = condition_variable.wait_for(lock, *next_time - now, [&] {
                return !running || !this->buffers.empty();
                });
              if (wait_result) {
                break;
              }
            }
            now = std::chrono::steady_clock::now();

            {
              //TRACE_EVENT0("thalamus", "NidaqOutputNode::write_signal(digital)");
              for (auto c = 0; c < buffers.size(); ++c) {
                auto& buffer = buffers.at(c);
                auto& time = times.at(c);
                auto& position = positions.at(c);

                std::optional<unsigned char> value;
                while (time <= now && position < buffer.size()) {
                  value = buffer.at(position) > 1.6;
                  ++position;
                  time += sample_intervals.at(c);
                }
                if (value) {
                  auto status = comedi_dio_write(device, subdevice, channels[c], *value);
                  THALAMUS_ASSERT(status >= 0, "comedi_dio_write failed: %d", status);
                  digital_levels.at(c) = *value;
                }
              }
            }
          }
        }
        else {
          while (!is_done()) {
            auto next_time = std::min_element(times.begin(), times.end());
            auto now = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(*next_time - now);
            if (new_buffers) {
              break;
            }
            now = std::chrono::steady_clock::now();

            {
              TRACE_EVENT0("thalamus", "NidaqOutputNode::write_signal(analog)");
              for (auto c = 0; c < buffers.size(); ++c) {
                auto& buffer = buffers.at(c);
                auto& time = times.at(c);
                auto& position = positions.at(c);
                std::optional<double> value;
                while (time <= now && position < buffer.size()) {
                  value = buffer.at(position);
                  ++position;
                  time += sample_intervals.at(c);
                }
                if (value) {
                  auto status = comedi_data_write(device, subdevice, channels[c], 0, 0, *value);
                  THALAMUS_ASSERT(status >= 0, "comedi_data_write failed: %d", status);
                }
              }
            }
          }
        }
      }
    }

    void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Running" || key_str == "Source" || key_str == "Channel" || key_str == "Digital") {
        {
          std::lock_guard<std::mutex> lock(mutex);
          running = state->at("Running");
        }
        condition_variable.notify_all();

        if (nidaq_thread.joinable()) {
          nidaq_thread.join();
        }
        close_device(&device);

        source_connection.disconnect();

        if (!state->contains("Source")) {
          return;
        }
        std::string source_str = state->at("Source");
        graph->get_node(source_str, [&](auto node) {
          source = node;
          auto locked_source = source.lock();
          if (!locked_source || dynamic_cast<AnalogNode*>(locked_source.get()) == nullptr) {
            source.reset();
            return;
          }
          source_connection = locked_source->ready.connect(std::bind(&Impl::on_data, this, _1));
        });

        if (running) {
          buffers.clear();
          counter = 0;
          //started = false;
          std::string name = state->at("name");
          std::string channel = state->at("Channel");
          std::string channel_name = name + " channel";
          _num_channels = NidaqNode::get_num_channels(channel);
          auto device_params = parse_device_string(channel);
          if(!device_params) {
            (*state)["Running"].assign(false, [&] {});
            return;
          }
          subdevice = device_params->subdevice;
          channels = device_params->channels;

          digital = is_digital(channel);
          if(!digital) {
            subdevice = comedi_find_subdevice_by_type(device, COMEDI_SUBD_AO, 0);
          }

          _sample_rate = -1;
          buffer_size = static_cast<size_t>(16 * _num_channels);
          std::function<void()> reader;
          nidaq_thread = std::thread(std::bind(&Impl::nidaq_target, this));

          auto filename = absl::StrFormat("/dev/comedi%d", device_params->device-1);
          device = comedi_open(filename.c_str());
          BOOST_ASSERT_MSG(device != nullptr, "comedi_open failed");

          if (digital) {
            for(auto channel : channels) {
              auto daq_error = comedi_dio_config(device, subdevice, channel, COMEDI_OUTPUT);
              BOOST_ASSERT_MSG(daq_error >= 0, "comedi_dio_config failed");
            }
          }
        }
        else {
          if (nidaq_thread.joinable()) {
            nidaq_thread.join();
          }
        }
      }
    }

    void on_data(Node* raw_node) {
      TRACE_EVENT0("thalamus", "NidaqOutputNode::on_data");
      if (device == nullptr) {
        return;
      }
      auto node = reinterpret_cast<AnalogNode*>(raw_node);
      _time = node->time();
      {
        std::lock_guard<std::mutex> lock(mutex);
        //buffers.assign(node->num_channels(), std::vector<double>());
        _data.clear();
        //_sample_intervals.clear();
        buffers.clear();
        _sample_intervals.clear();
        for (auto i = 0; i < node->num_channels(); ++i) {
          auto data = node->data(i);
          buffers.emplace_back(data.begin(), data.end());
          _data.push_back(data);
          _sample_intervals.emplace_back(node->sample_interval(i));
        }
        new_buffers = true;
      }
      condition_variable.notify_all();

      outer->ready(outer);
    }
  };

  NidaqOutputNode::NidaqOutputNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph) : impl(new Impl(state, io_context, graph, this)) {}
  NidaqOutputNode::~NidaqOutputNode() {}

  std::string NidaqOutputNode::type_name() {
    return "NIDAQ_OUT (COMEDI)";
  }

  bool NidaqNode::prepare() {
    return true;
  }

  bool NidaqOutputNode::prepare() {
    return true;
  }

}
