#include "libs/asio/include/boost/asio/io_context.hpp"
#include <nidaq_node.h>
#include <regex>
#include <absl/strings/numbers.h>
#include <tracing/tracing.h>
#include <numeric>
#include <grpc_impl.h>
#include <comedilib.h>
#include <modalities_util.h>

namespace thalamus {
  static void close_device(comedi_t** device) {
    if(*device != nullptr) {
      comedi_close(*device);
      *device = nullptr;
    }
  }

  struct DeviceParams {
    int device = -1;
    int subdevice = -1;
    thalamus::vector<unsigned int> channels;
  };

  bool is_digital(const std::string& channel) {
    return channel.find("port") != std::string::npos;
  }

  static std::regex nidaq_regex("^([a-zA-Z0-9/]+)(\\d+)(:(\\d+))?$");

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
      THALAMUS_ASSERT(success, "channel range parse failed");
      success = absl::SimpleAtoi(right_str, &right);
      THALAMUS_ASSERT(success, "channel range parse failed");
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
    auto success = absl::SimpleAtoi(match_result[1].str(), &result.device);
    THALAMUS_ASSERT(success, "absl::SimpleAtoi failed");

    if(match_result[6].matched) {
      success = absl::SimpleAtoi(match_result[3].str(), &result.subdevice);
      THALAMUS_ASSERT(success, "absl::SimpleAtoi failed");
      auto left_str = match_result[7].str();
      auto right_str = match_result[8].matched ? match_result[9].str() : left_str;
      int left, right;
      success = absl::SimpleAtoi(left_str, &left);
      THALAMUS_ASSERT(success, "absl::SimpleAtoi failed");
      success = absl::SimpleAtoi(right_str, &right);
      THALAMUS_ASSERT(success, "absl::SimpleAtoi failed");
      for(auto i = left;i < right+1;++i) {
        result.channels.push_back(i);
      }
    } else {
      auto left_str = match_result[3].str();
      auto right_str = match_result[4].matched ? match_result[5].str() : left_str;
      int left, right;
      success = absl::SimpleAtoi(left_str, &left);
      THALAMUS_ASSERT(success, "absl::SimpleAtoi failed");
      success = absl::SimpleAtoi(right_str, &right);
      THALAMUS_ASSERT(success, "absl::SimpleAtoi failed");
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
    std::vector<std::vector<double>> output_data;
    size_t complete_samples = 0;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, NidaqNode* outer)
      : state(state)
      , device(nullptr)
      , io_context(io_context)
      , timer(io_context)
      , analog_buffer_position(0)
      , busy(false)
      , graph(graph)
      , outer(outer)
      , is_running(false) {
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [] {});
      close_device(&device);
    }

    std::vector<comedi_range *> range_info;
    std::vector<lsampl_t> maxdata;
    std::vector<unsigned int> chanlist;
    comedi_cmd command;
    lsampl_t lsampl_buffer[16384];
    sampl_t* sampl_buffer = reinterpret_cast<sampl_t*>(lsampl_buffer);
    unsigned char* bytes_buffer = reinterpret_cast<unsigned char*>(lsampl_buffer);
    size_t buffer_size = sizeof(lsampl_buffer);
    size_t offset = 0;

    unsigned int flags;
    size_t bytes_per_sample;
    size_t next_channel = 0;

    void store_sample(unsigned int sample) {
      auto physical_value = comedi_to_phys(sample, range_info[next_channel], maxdata[next_channel]);
      output_data[next_channel].push_back(physical_value);
      next_channel = (next_channel+1) % output_data.size();
    }

    void on_timer_digital(std::chrono::milliseconds polling_interval, std::chrono::nanoseconds sample_interval, std::chrono::steady_clock::time_point last_sample, const boost::system::error_code& e) {
      if(e) {
        THALAMUS_LOG(info) << e.what();
        (*state)["Running"].assign(false, [] {});
        return;
      }

      unsigned int bits;
      auto ret = comedi_dio_bitfield2(device, subdevice, 0, &bits, 0);
      if(ret < 0) {
        THALAMUS_LOG(error) << strerror(errno);
        (*state)["Running"].assign(false, [] {});
        return;
      }

      auto now = std::chrono::steady_clock::now();

      while(last_sample < now) {
        for(auto i = 0ull;i < channels.size();++i) {
          auto channel = channels[i];
          auto high = bits & (0x1 << channel);
          output_data[i].push_back(high ? 5 : 0);
        }
        last_sample += sample_interval;
      }

      complete_samples = output_data.front().size();
      if(complete_samples*sample_interval >= polling_interval) {
        _time = now.time_since_epoch();
        outer->ready(outer);
        for(auto& d : output_data) {
          d.clear();
        }
      }


      timer.expires_after(sample_interval);
      timer.async_wait(std::bind(&Impl::on_timer_digital, this, polling_interval, sample_interval, last_sample, _1));
    }

    void on_timer_analog(std::chrono::milliseconds polling_interval, const boost::system::error_code& e) {
      if(e) {
        THALAMUS_LOG(info) << e.what();
        (*state)["Running"].assign(false, [] {});
        return;
      }

		  auto ret = read(comedi_fileno(device),bytes_buffer+offset,buffer_size-offset);
      if(ret < 0) {
        if(errno==EAGAIN) {
          timer.expires_after(polling_interval);
          timer.async_wait(std::bind(&Impl::on_timer_analog, this, polling_interval, _1));
        } else {
          THALAMUS_LOG(error) << strerror(errno);
          (*state)["Running"].assign(false, [] {});
        }
        return;
      } else if(ret == 0) {
        THALAMUS_LOG(error) << "End of comedi stream";
        (*state)["Running"].assign(false, [] {});
        return;
      }

      _time = std::chrono::steady_clock::now().time_since_epoch();

      for(auto& d : output_data) {
        d.erase(d.begin(), d.begin() + complete_samples);
      }

      auto end = (offset+ret)/bytes_per_sample;
      if(bytes_per_sample == sizeof(lsampl_t)) {
        for(auto i = 0u;i < end;++i) {
          store_sample(lsampl_buffer[i]);
        }
      } else {
        for(auto i = 0u;i < end;++i) {
          store_sample(sampl_buffer[i]);
        }
      }

      complete_samples = std::numeric_limits<size_t>::max();
      for(auto& d : output_data) {
        complete_samples = std::min(complete_samples, d.size());
      }

      if(complete_samples) {
        outer->ready(outer);
      }

      std::copy(bytes_buffer+end*bytes_per_sample, bytes_buffer+offset+ret, bytes_buffer);
      offset = (offset+ret) % bytes_per_sample;

      timer.expires_after(polling_interval);
      timer.async_wait(std::bind(&Impl::on_timer_analog, this, polling_interval, _1));
    }

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Channel") {
        std::string channel = state->at("Channel");
        recommended_names = get_channels(channel);
        _num_channels = recommended_names.size();
      }
      else if (key_str == "Running") {
        timer.cancel();
        close_device(&device);
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

          auto filename = absl::StrFormat("/dev/comedi%d", device_params->device-1);
          device = comedi_open(filename.c_str());
          THALAMUS_ASSERT(device != nullptr, "comedi_open failed");
          fcntl(comedi_fileno(device), F_SETFL, O_NONBLOCK);

          digital = is_digital(channel);
          if(!digital) {
            subdevice = comedi_get_read_subdevice(device);
            THALAMUS_ASSERT(subdevice >= 0, "comedi_get_read_subdevice failed");
          }

          output_data.resize(channels.size());
          for(auto& d : output_data) {
            d.clear();
          }
          complete_samples = 0;

          _time = 0ns;
          outer->channels_changed(outer);
          if (digital) {
            for(auto channel : channels) {
              auto daq_error = comedi_dio_config(device, subdevice, channel, COMEDI_INPUT);
              THALAMUS_ASSERT(daq_error >= 0, "DIO config failed");
            }

            on_timer_digital(polling_interval, _sample_interval, std::chrono::steady_clock::now(), boost::system::error_code());
          } else {
            range_info.clear();
            maxdata.clear();
            chanlist.clear();
            for(auto channel : channels) {
              chanlist.push_back(CR_PACK(channel, 0, AREF_GROUND));
		          range_info.push_back(comedi_get_range(device, subdevice, channel, 0));
              THALAMUS_ASSERT(range_info.back(), "comedi_get_range failed");
		          maxdata.push_back(comedi_get_maxdata(device, subdevice, channel));
              THALAMUS_ASSERT(maxdata.back(), "comedi_get_maxdata failed");
            }
            memset(&command, 0, sizeof(command));
	          auto ret = comedi_get_cmd_generic_timed(device, subdevice, &command, channels.size(), _sample_interval.count());
            THALAMUS_ASSERT(ret == 0, "comedi_get_cmd_generic_timed failed");
	          command.chanlist = chanlist.data();
           	command.chanlist_len = chanlist.size();
            command.stop_src = TRIG_NONE;
            command.flags = TRIG_WAKE_EOS;
            ret = comedi_command_test(device, &command);
            THALAMUS_ASSERT(ret >= 0, "comedi_command_test failed");
            ret = comedi_command_test(device, &command);
            THALAMUS_ASSERT(ret == 0, "comedi_command_test failed");
	          ret = comedi_set_read_subdevice(device, subdevice);
            THALAMUS_ASSERT(ret == 0, "comedi_set_read_subdevice failed");
	          ret = comedi_command(device, &command);
            THALAMUS_ASSERT(ret == 0, "comedi_command failed");

            flags = comedi_get_subdevice_flags(device, subdevice);
            bytes_per_sample = flags & SDF_LSAMPL ? sizeof(lsampl_t) : sizeof(sampl_t);
            next_channel = 0;
            offset = 0;

            on_timer_analog(polling_interval, boost::system::error_code());
          }
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
      auto success = absl::SimpleAtoi(left_str, &left);
      THALAMUS_ASSERT(success, "channel range parse failed");
      success = absl::SimpleAtoi(right_str, &right);
      THALAMUS_ASSERT(success, "channel range parse failed");
      return right - left + 1;
    }
  }

  std::string NidaqNode::type_name() {
    return "NIDAQ (COMEDI)";
  }

  std::span<const double> NidaqNode::data(int channel) const {
    return std::span<const double>(impl->output_data[channel].begin(), impl->output_data[channel].begin() + impl->complete_samples);
  }

  int NidaqNode::num_channels() const {
    return impl->output_data.size();
  }

  std::chrono::nanoseconds NidaqNode::sample_interval(int) const {
    return impl->_sample_interval;
  }

  std::chrono::nanoseconds NidaqNode::time() const {
    return impl->_time;
  }

  std::string_view NidaqNode::name(int channel) const {
    return impl->recommended_names.at(channel);
  }

  void NidaqNode::inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
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
      , graph(graph)
      , io_context(io_context)
      , timer(io_context)
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
          for (auto i = 0ull; i < buffers.size(); ++i) {
            result = result && positions.at(i) == buffers.at(i).size();
          }
          return result;
        };

        if (digital) {
          while (!is_done()) {
            {
              //TRACE_EVENT0("thalamus", "NidaqOutputNode::fast_forward(digital)");
              for (auto c = 0ull; c < buffers.size(); ++c) {
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
              for (auto c = 0ull; c < buffers.size(); ++c) {
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
              for (auto c = 0ull; c < buffers.size(); ++c) {
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

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value&) {
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
          if (!locked_source || node_cast<AnalogNode*>(locked_source.get()) == nullptr) {
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

  size_t NidaqNode::modalities() const { return infer_modalities<NidaqNode>(); }
}
