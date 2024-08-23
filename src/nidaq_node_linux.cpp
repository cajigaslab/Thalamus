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
    AnalogNode* source;
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
    std::atomic_bool running;
    std::vector<std::chrono::steady_clock::time_point> next_write;
    std::vector<std::chrono::steady_clock::time_point> times;
    bool digital = false;
    std::vector<bool> digital_levels;
    std::atomic_bool new_buffers = false;
    size_t bytes_per_sample;

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

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value&) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Running" || key_str == "Source" || key_str == "Channel" || key_str == "Digital") {
        running = state->at("Running");
        close_device(&device);

        source_connection.disconnect();

        if (!state->contains("Source")) {
          return;
        }
        std::string source_str = state->at("Source");
        graph->get_node(source_str, [&](auto node) {
          auto locked_source = node.lock();
          source = locked_source ? node_cast<AnalogNode*>(locked_source.get()) : nullptr;
          if (!source) {
            source = nullptr;
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

          auto filename = absl::StrFormat("/dev/comedi%d", device_params->device-1);
          device = comedi_open(filename.c_str());
          THALAMUS_ASSERT(device != nullptr, "comedi_open failed");

          digital = is_digital(channel);
          if(!digital) {
            subdevice = comedi_get_write_subdevice(device);
            THALAMUS_ASSERT(subdevice >= 0, "comedi_get_read_subdevice failed");
          }

          auto ret = comedi_cancel(device, subdevice);
          THALAMUS_ASSERT(ret == 0, "comedi_cancel failed");

          if (digital) {
            for(auto channel : channels) {
              auto daq_error = comedi_dio_config(device, subdevice, channel, COMEDI_OUTPUT);
              THALAMUS_ASSERT(daq_error >= 0, "DIO config failed");
            }
          } else {
            range_info.clear();
            maxdata.clear();
            chanlist.clear();
            for(auto channel : channels) {
              chanlist.push_back(CR_PACK(channel, 0, AREF_GROUND));
		          range_info.push_back(comedi_get_range(device, subdevice, channel, 0));
              THALAMUS_ASSERT(range_info.back(), "comedi_get_range failed: %d %d", subdevice, channel);
		          maxdata.push_back(comedi_get_maxdata(device, subdevice, channel));
              THALAMUS_ASSERT(maxdata.back(), "comedi_get_maxdata failed");
            }
            memset(&command,0,sizeof(command));
            command.subdev = subdevice;
            command.flags = CMDF_WRITE;
            command.start_src = TRIG_INT;
            command.start_arg = 0;
            command.scan_begin_src = TRIG_TIMER;
            command.scan_begin_arg = 1;
            command.convert_src = TRIG_NOW;
            command.convert_arg = 0;
            command.scan_end_src = TRIG_COUNT;
            command.scan_end_arg = channels.size();
            command.stop_src = TRIG_COUNT;
            command.stop_arg = 1;
            command.chanlist = chanlist.data();
            command.chanlist_len = chanlist.size();
            ret = comedi_command_test(device, &command);
            THALAMUS_LOG(info) << "scan_begin_arg " << command.scan_begin_arg;
            THALAMUS_ASSERT(ret >= 0, "comedi_command_test failed");
            ret = comedi_command_test(device, &command);
            THALAMUS_ASSERT(ret == 0, "comedi_command_test failed");
            
	          ret = comedi_set_write_subdevice(device, subdevice);
            THALAMUS_ASSERT(ret == 0, "comedi_set_write_subdevice failed");
	          THALAMUS_LOG(info) << "comedi_get_write_subdevice " << comedi_get_write_subdevice(device);

	          //ret = comedi_command(device, &command);
            //THALAMUS_ASSERT(ret == 0, "comedi_command failed");

            auto flags = comedi_get_subdevice_flags(device, subdevice);
            bytes_per_sample = flags & SDF_LSAMPL ? sizeof(lsampl_t) : sizeof(sampl_t);

            buffer.assign(channels.size(), 0);
            current_values.assign(channels.size(), 0);
            //lsampls = buffer.data();
            //sampls = reinterpret_cast<sampl_t*>(lsampls);
            //sampls[0] = 12345;
            
	          //ret = write(comedi_fileno(device), (void*)buffer.data(), buffer.size()*bytes_per_sample);
            //THALAMUS_ASSERT(ret == buffer.size()*bytes_per_sample, "write failed");
	          //ret = comedi_internal_trigger(device, subdevice, 0);
            //comedi_perror("comedi_internal_trigger");
            //THALAMUS_ASSERT(ret >= 0, "comedi_internal_trigger failed %s", strerror(errno));

            //comedi_set_buffer_size(device, subdevice, bytes_per_sample*channels.size()*100000);
  
            looping = false;
            nidaq_thread = std::thread(std::bind(&Impl::nidaq_target, this));
          }
        } else {
          if(nidaq_thread.joinable()) {
            nidaq_thread.join();
          }
        }
      }
    }

    void nidaq_target() {
      std::vector<sampl_t> copied_values;
      while(running) {
        auto ret = comedi_cancel(device, subdevice);
        THALAMUS_ASSERT(ret == 0, "comedi_cancel failed");

	      ret = comedi_command(device, &command);
        THALAMUS_ASSERT(ret == 0, "comedi_command failed");

        {
          std::lock_guard<std::mutex> lock(mutex);
          copied_values = current_values;
        }

        ret = write(comedi_fileno(device), (void*)copied_values.data(), copied_values.size()*bytes_per_sample);
        THALAMUS_ASSERT(ret >= 0, "write failed %s", strerror(errno));

	      ret = comedi_internal_trigger(device, subdevice, 0);
        THALAMUS_ASSERT(ret >= 0, "comedi_internal_trigger failed");
      }
    }

    bool looping;
    
    std::vector<sampl_t> current_values;

    std::vector<comedi_range *> range_info;
    std::vector<lsampl_t> maxdata;
    std::vector<unsigned int> chanlist;
    std::vector<sampl_t> buffer;
    //lsampl_t* lsampls;
    //sampl_t* sampls;
    comedi_cmd command;

    void on_data(Node*) {
      static int cc = 0;
      static auto tt = std::chrono::steady_clock::now();
      TRACE_EVENT0("thalamus", "NidaqOutputNode::on_data");
      if(!source->has_analog_data() || device == nullptr) {
        return;
      }
      if(!running) {
        return;
      }
      if(digital) {
        unsigned int bits = 0;
        for (auto i = 0; i < source->num_channels(); ++i) {
          auto data = source->data(i);
          auto channel = channels[i];
          if(!data.empty()) {
            bits |= data.back() > 1 ? (0x1 << channel) : 0;
          }
          auto ret = comedi_dio_bitfield2(device, subdevice, std::numeric_limits<unsigned int>::max(), &bits, 0);
          if(ret < 0) {
            THALAMUS_LOG(error) << strerror(errno);
            running = false;
            (*state)["Running"].assign(false, [] {});
            return;
          }
        }
      } else {
        auto count = std::min(size_t(source->num_channels()), channels.size());
        std::lock_guard<std::mutex> lock(mutex);
        for (auto i = 0ull; i < count; ++i) {
          auto data = source->data(i);
          if(!data.empty()) {
            auto sample = comedi_from_phys(data.back(), range_info[i], maxdata[i]);
            current_values[i] = sample;

            //auto ret = comedi_cancel(device, subdevice);
            //THALAMUS_ASSERT(ret == 0, "comedi_cancel failed");

            //auto ret = write(comedi_fileno(device), (void*)buffer.data(), buffer.size()*bytes_per_sample);
            ////THALAMUS_ASSERT(ret == buffer.size()*bytes_per_sample, "write failed");
	          //ret = comedi_internal_trigger(device, subdevice, 0);
            //++cc;
            //auto now = std::chrono::steady_clock::now();
            //if(now - tt > 1s) {
            //  THALAMUS_LOG(info) << cc;
            //  tt = now;
            //  cc = 0;
            //}
            //THALAMUS_LOG(info) << "trigger " << ret << " " << device << " " << subdevice << " " << strerror(errno);
            //THALAMUS_ASSERT(ret >= 0, "comedi_internal_trigger failed");
            //if(bytes_per_sample == sizeof(lsampl_t)) {
            //  lsampls[i] = sample;
            //} else {
            //  sampls[i] = sample;
            //}
          }
        }
      }
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
