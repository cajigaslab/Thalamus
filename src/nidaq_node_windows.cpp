#include <base64.hpp>
#include <cstdint>
#include <grpc_impl.hpp>
#include <modalities_util.hpp>
#include <nidaq_node.hpp>
#include <numeric>
#include <regex>
#include <thalamus/thread.hpp>
#include <thalamus/tracing.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <NIDAQmx.h>
#include <Windows.h>
#include <absl/strings/numbers.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
static ::HMODULE nidaq_dll_handle;

struct DAQmxAPI {
  bool loaded = false;
  decltype(&::DAQmxStartTask) DAQmxStartTask;
  decltype(&::DAQmxStopTask) DAQmxStopTask;
  decltype(&::DAQmxClearTask) DAQmxClearTask;
  decltype(&::DAQmxReadAnalogF64) DAQmxReadAnalogF64;
  decltype(&::DAQmxCreateTask) DAQmxCreateTask;
  decltype(&::DAQmxCreateAIVoltageChan) DAQmxCreateAIVoltageChan;
  decltype(&::DAQmxCfgSampClkTiming) DAQmxCfgSampClkTiming;
  decltype(&::DAQmxRegisterEveryNSamplesEvent) DAQmxRegisterEveryNSamplesEvent;
  decltype(&::DAQmxWriteDigitalLines) DAQmxWriteDigitalLines;
  decltype(&::DAQmxWriteAnalogScalarF64) DAQmxWriteAnalogScalarF64;
  decltype(&::DAQmxWriteAnalogF64) DAQmxWriteAnalogF64;
  decltype(&::DAQmxCreateDOChan) DAQmxCreateDOChan;
  decltype(&::DAQmxCreateAOVoltageChan) DAQmxCreateAOVoltageChan;
  decltype(&::DAQmxRegisterDoneEvent) DAQmxRegisterDoneEvent;
  decltype(&::DAQmxCfgDigEdgeStartTrig) DAQmxCfgDigEdgeStartTrig;
  decltype(&::DAQmxSetBufInputBufSize) DAQmxSetBufInputBufSize;
};
static DAQmxAPI *daqmxapi;

static bool prepare_nidaq() {
  static bool has_run = false;
  if (has_run) {
    return daqmxapi->loaded;
  }
  daqmxapi = new DAQmxAPI();
  has_run = true;
  nidaq_dll_handle = LoadLibrary("nicaiu");
  if (!nidaq_dll_handle) {
    THALAMUS_LOG(info)
        << "Couldn't find nicaiu.dll.  National Instruments features disabled";
    return false;
  }
  THALAMUS_LOG(info) << "nicaiu.dll found.  Loading DAQmx API";

  std::string nidaq_dll_path(256, ' ');
  DWORD filename_size = uint32_t(nidaq_dll_path.size());
  while (nidaq_dll_path.size() == filename_size) {
    nidaq_dll_path.resize(2 * nidaq_dll_path.size(), ' ');
    filename_size = GetModuleFileNameA(nidaq_dll_handle, nidaq_dll_path.data(),
                                       uint32_t(nidaq_dll_path.size()));
  }
  if (filename_size == 0) {
    THALAMUS_LOG(warning) << "Error while finding nicaiu.dll absolute path";
  } else {
    nidaq_dll_path.resize(filename_size);
    THALAMUS_LOG(info) << "Absolute nicaiu.dll path = " << nidaq_dll_path;
  }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#endif
  daqmxapi->DAQmxStartTask = reinterpret_cast<decltype(&DAQmxStartTask)>(
      ::GetProcAddress(nidaq_dll_handle, "DAQmxStartTask"));
  if (!daqmxapi->DAQmxStartTask) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxStartTask.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxStopTask = reinterpret_cast<decltype(&DAQmxStopTask)>(
      ::GetProcAddress(nidaq_dll_handle, "DAQmxStopTask"));
  if (!daqmxapi->DAQmxStopTask) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxStopTask .  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxClearTask = reinterpret_cast<decltype(&DAQmxClearTask)>(
      ::GetProcAddress(nidaq_dll_handle, "DAQmxClearTask"));
  if (!daqmxapi->DAQmxClearTask) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxClearTask .  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxReadAnalogF64 =
      reinterpret_cast<decltype(&DAQmxReadAnalogF64)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxReadAnalogF64"));
  if (!daqmxapi->DAQmxReadAnalogF64) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxReadAnalogF64 .  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxCreateTask = reinterpret_cast<decltype(&DAQmxCreateTask)>(
      ::GetProcAddress(nidaq_dll_handle, "DAQmxCreateTask"));
  if (!daqmxapi->DAQmxCreateTask) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxCreateTask.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxCreateAIVoltageChan =
      reinterpret_cast<decltype(&DAQmxCreateAIVoltageChan)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxCreateAIVoltageChan"));
  if (!daqmxapi->DAQmxCreateAIVoltageChan) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxCreateAIVoltageChan.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxCfgSampClkTiming =
      reinterpret_cast<decltype(&DAQmxCfgSampClkTiming)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxCfgSampClkTiming"));
  if (!daqmxapi->DAQmxCfgSampClkTiming) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxCfgSampClkTiming.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxRegisterEveryNSamplesEvent =
      reinterpret_cast<decltype(&DAQmxRegisterEveryNSamplesEvent)>(
          ::GetProcAddress(nidaq_dll_handle,
                           "DAQmxRegisterEveryNSamplesEvent"));
  if (!daqmxapi->DAQmxRegisterEveryNSamplesEvent) {
    THALAMUS_LOG(info) << "Failed to load DAQmxRegisterEveryNSamplesEvent.  NI "
                          "features disabled";
    return false;
  }
  daqmxapi->DAQmxWriteDigitalLines =
      reinterpret_cast<decltype(&DAQmxWriteDigitalLines)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxWriteDigitalLines"));
  if (!daqmxapi->DAQmxWriteDigitalLines) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxWriteDigitalLines.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxWriteAnalogF64 =
      reinterpret_cast<decltype(&DAQmxWriteAnalogF64)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxWriteAnalogF64"));
  if (!daqmxapi->DAQmxWriteAnalogF64) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxWriteAnalogF64.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxWriteAnalogScalarF64 =
      reinterpret_cast<decltype(&DAQmxWriteAnalogScalarF64)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxWriteAnalogScalarF64"));
  if (!daqmxapi->DAQmxWriteAnalogScalarF64) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxWriteAnalogScalarF64.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxCreateDOChan = reinterpret_cast<decltype(&DAQmxCreateDOChan)>(
      ::GetProcAddress(nidaq_dll_handle, "DAQmxCreateDOChan"));
  if (!daqmxapi->DAQmxCreateDOChan) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxCreateDOChan.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxCreateAOVoltageChan =
      reinterpret_cast<decltype(&DAQmxCreateAOVoltageChan)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxCreateAOVoltageChan"));
  if (!daqmxapi->DAQmxCreateAOVoltageChan) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxCreateAOVoltageChan.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxRegisterDoneEvent =
      reinterpret_cast<decltype(&DAQmxRegisterDoneEvent)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxRegisterDoneEvent"));
  if (!daqmxapi->DAQmxRegisterDoneEvent) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxRegisterDoneEvent.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxCfgDigEdgeStartTrig =
      reinterpret_cast<decltype(&DAQmxCfgDigEdgeStartTrig)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxCfgDigEdgeStartTrig"));
  if (!daqmxapi->DAQmxCfgDigEdgeStartTrig) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxCfgDigEdgeStartTrig.  NI features disabled";
    return false;
  }
  daqmxapi->DAQmxSetBufInputBufSize =
      reinterpret_cast<decltype(&DAQmxSetBufInputBufSize)>(
          ::GetProcAddress(nidaq_dll_handle, "DAQmxSetBufInputBufSize"));
  if (!daqmxapi->DAQmxSetBufInputBufSize) {
    THALAMUS_LOG(info)
        << "Failed to load DAQmxSetBufInputBufSize.  NI features disabled";
    return false;
  }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  daqmxapi->loaded = true;
  THALAMUS_LOG(info) << "DAQmx API loaded";
  return true;
}

static thalamus::vector<std::string> get_channels(const std::string &channel) {
  std::regex nidaq_regex("([a-zA-Z0-9/]+)(\\d+)(:(\\d+))?$");

  std::smatch match_result;
  if (!std::regex_search(channel, match_result, nidaq_regex)) {
    // QMessageBox::warning(nullptr, "Parse Failed", "Failed to parse NIDAQ
    // channel");
    return thalamus::vector<std::string>();
  }
  if (!match_result[4].matched) {
    return thalamus::vector<std::string>(1, channel);
  } else {
    auto base_str = match_result[1].str();
    auto left_str = match_result[2].str();
    auto right_str = match_result[4].str();
    int left, right;
    auto success = absl::SimpleAtoi(left_str, &left);
    THALAMUS_ASSERT(success);
    success = absl::SimpleAtoi(right_str, &right);
    THALAMUS_ASSERT(success);
    thalamus::vector<std::string> result;
    for (auto i = left; i <= right; ++i) {
      result.push_back(base_str + std::to_string(i));
    }
    return result;
  }
}

struct NidaqNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  TaskHandle task_handle;
  boost::asio::io_context &io_context;
  boost::asio::high_resolution_timer timer;
  size_t analog_buffer_position;
  std::vector<double> analog_buffer;
  std::vector<double> output_buffer;
  std::vector<std::span<double const>> spans;
  size_t _num_channels;
  size_t buffer_size;
  std::chrono::nanoseconds _sample_interval;
  size_t counter = 0;
  std::chrono::nanoseconds _time = 0ns;
  std::atomic_bool busy;
  int32 _every_n_samples;
  std::list<std::vector<double>> buffers;
  NodeGraph *graph;
  NidaqNode *outer;
  bool is_running;
  thalamus::vector<std::string> recommended_names;
  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, NidaqNode *_outer)
      : state(_state), task_handle(nullptr), io_context(_io_context),
        timer(_io_context), analog_buffer_position(0), busy(false),
        graph(_graph), outer(_outer), is_running(false) {
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [] {});
#ifdef _WIN32
    if (task_handle != nullptr) {
      daqmxapi->DAQmxStopTask(task_handle);
      daqmxapi->DAQmxClearTask(task_handle);
      task_handle = nullptr;
    }
#endif
  }

  static int32 CVICALLBACK NidaqCallback(TaskHandle task_handle, int32, uInt32,
                                         void *callbackData) {
#ifdef _WIN32
    TRACE_EVENT("thalamus", "NidaqCallback");
    auto now = std::chrono::steady_clock::now();
    auto weak_ptr = static_cast<std::weak_ptr<Node> *>(callbackData);
    auto locked_ptr = weak_ptr->lock();
    if (!locked_ptr) {
      return 0;
    }
    auto node = static_cast<NidaqNode *>(locked_ptr.get());
    auto &impl = node->impl;
    std::vector<double> buffer(impl->_num_channels *
                               size_t(impl->_every_n_samples));

    int32 num_samples;
    auto daq_error = daqmxapi->DAQmxReadAnalogF64(
        task_handle, impl->_every_n_samples, 10, DAQmx_Val_GroupByChannel,
        buffer.data(), uint32_t(buffer.size()), &num_samples, nullptr);

    BOOST_ASSERT(daq_error >= 0);
    impl->counter += size_t(num_samples);

    boost::asio::post(impl->io_context, [node, moved_buffer = std::move(buffer),
                                         now]() {
      TRACE_EVENT("thalamus", "NidaqCallback(post)");
      auto &node_impl = node->impl;
      node_impl->output_buffer = std::move(moved_buffer);
      node_impl->spans.clear();
      for (auto channel = 0u; channel < node_impl->_num_channels; ++channel) {
        node_impl->spans.emplace_back(
            node_impl->output_buffer.begin() +
                channel * uint32_t(node_impl->_every_n_samples),
            node_impl->output_buffer.begin() +
                (channel + 1) * uint32_t(node_impl->_every_n_samples));
      }

      node_impl->_time = now.time_since_epoch();
      node->ready(node);
      node_impl->busy = false;
    });

    return 0;
#else

    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);
    if (!is_running) {
      return;
    }

    output_buffer.resize(this->_num_channels * this->_every_n_samples);

    for (auto i = 0; i < this->_every_n_samples; ++i) {
      for (auto c = 0u; c < this->_num_channels; ++c) {
        if (c == 0) {
          output_buffer[c * this->_every_n_samples + i] =
              std::sin(this->_time.count() / 1e9);
        } else {
          output_buffer[c * this->_every_n_samples + i] =
              std::sin(this->_time.count() / 1e9 + M_PI / 4);
        }
      }
      this->_time += this->_sample_interval;
    }
    spans.clear();
    for (auto channel = 0; channel < _num_channels; ++channel) {
      spans.emplace_back(output_buffer.begin() + channel * _every_n_samples,
                         output_buffer.begin() +
                             (channel + 1) * _every_n_samples);
    }

    this->ready(this);

    this->timer.expires_after(_polling_interval);
    this->timer.async_wait(std::bind(&NidaqNode::NidaqCallback, this, _1));
    return 0;
#endif
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "NidaqNode::on_change");
#ifdef _WIN32
    auto key_str = std::get<std::string>(k);
    if (key_str == "Channel") {
      std::string channel = state->at("Channel");
      recommended_names = get_channels(channel);
      _num_channels = size_t(get_num_channels(channel));
    } else if (key_str == "Running") {
      auto current_is_running = std::get<bool>(v);
      if (current_is_running) {
        counter = 0;
        std::string name = state->at("name");
        std::string channel = state->at("Channel");
        double sample_rate = state->at("Sample Rate");

        _sample_interval = std::chrono::nanoseconds(size_t(1e9 / sample_rate));

        size_t polling_interval_raw = state->at("Poll Interval");
        std::chrono::milliseconds polling_interval(polling_interval_raw);

        _every_n_samples = int(polling_interval / _sample_interval);

        std::string channel_name = name + " channel";
        buffer_size = 2 * size_t(_every_n_samples) * _num_channels;
        std::function<void()> reader;

        auto daq_error = daqmxapi->DAQmxCreateTask(name.c_str(), &task_handle);
        BOOST_ASSERT_MSG(daq_error >= 0, "DAQmxCreateTask failed");

        daq_error = daqmxapi->DAQmxCreateAIVoltageChan(
            task_handle, channel.c_str(), channel_name.c_str(),
            DAQmx_Val_Cfg_Default, -10.0, 10.0, DAQmx_Val_Volts, nullptr);
        THALAMUS_ASSERT(daq_error >= 0, "DAQmxCreateAIVoltageChan failed %d",
                        daq_error);
        analog_buffer.resize(buffer_size);

        daq_error = daqmxapi->DAQmxCfgSampClkTiming(
            task_handle, nullptr, sample_rate, DAQmx_Val_Rising,
            DAQmx_Val_ContSamps, buffer_size);
        BOOST_ASSERT_MSG(daq_error >= 0, "DAQmxCfgSampClkTiming failed");

        daq_error = daqmxapi->DAQmxRegisterEveryNSamplesEvent(
            task_handle, DAQmx_Val_Acquired_Into_Buffer,
            uint32_t(_every_n_samples), 0, NidaqCallback,
            new std::weak_ptr<Node>(outer->weak_from_this()));
        BOOST_ASSERT_MSG(daq_error >= 0, "DAQmxCfgSampClkTiming failed");

        daq_error = daqmxapi->DAQmxSetBufInputBufSize(task_handle,
                                                      uint32_t(buffer_size));
        BOOST_ASSERT_MSG(daq_error >= 0, "DAQmxSetBufInputBufSize failed");

        daq_error = daqmxapi->DAQmxStartTask(task_handle);
        if (daq_error == DAQmxErrorPALResourceReserved) {
          THALAMUS_LOG(error) << "Channel in use: " << channel;
          state->at("Running").assign(false);
          daqmxapi->DAQmxClearTask(task_handle);
          task_handle = nullptr;
          return;
        } else if (daq_error < 0) {
          THALAMUS_LOG(error) << "DAQmxStartTask failed " << daq_error;
          state->at("Running").assign(false);
          daqmxapi->DAQmxClearTask(task_handle);
          task_handle = nullptr;
          return;
        }
        BOOST_ASSERT_MSG(daq_error >= 0, "DAQmxStartTask failed");
        _time = 0ns;
        outer->channels_changed(outer);

        // if (reader) {
        //   on_timer(reader, polling_interval, boost::system::error_code());
        // }
      } else if (task_handle != nullptr) {
        daqmxapi->DAQmxStopTask(task_handle);
        daqmxapi->DAQmxClearTask(task_handle);
        task_handle = nullptr;
        // timer.cancel();
      }
    }
#else
    auto key_str = std::get<std::string>(k);
    if (key_str == "Running") {
      is_running = std::get<bool>(v);
      if (is_running) {
        counter = 0;
        std::string channel = state->at("Channel");
        _sample_rate = state->at("Sample Rate");
        _sample_interval =
            std::chrono::nanoseconds(size_t(1 / _sample_rate * 1e9));
        _polling_interval =
            std::chrono::nanoseconds(state->at("Poll Interval"));
        _every_n_samples = int(_sample_rate * _polling_interval / 1000);
        _num_channels = get_num_channels(channel);
        buffer_size = static_cast<size_t>(_sample_rate * _num_channels);
        std::function<void()> reader;

        _time = 0ns;

        NidaqCallback(boost::system::error_code());
      }
    }
#endif
  }
};

NidaqNode::NidaqNode(ObservableDictPtr state,
                     boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

NidaqNode::~NidaqNode() {}

int NidaqNode::get_num_channels(const std::string &channel) {
  return int(get_channels(channel).size());
}

std::string NidaqNode::type_name() {
#ifdef _WIN32
  return "NIDAQ (NIDAQMX)";
#else
  return "NIDAQ (MOCK)";
#endif
}

std::span<const double> NidaqNode::data(int channel) const {
  return impl->spans.at(size_t(channel));
}

int NidaqNode::num_channels() const { return int(impl->_num_channels); }

std::chrono::nanoseconds NidaqNode::sample_interval(int) const {
  return impl->_sample_interval;
}

std::chrono::nanoseconds NidaqNode::time() const { return impl->_time; }

std::string_view NidaqNode::name(int channel) const {
  return impl->recommended_names.at(size_t(channel));
}

void NidaqNode::inject(
    const thalamus::vector<std::span<double const>> &spans,
    const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
    const thalamus::vector<std::string_view> &) {
  auto temp = impl->_num_channels;
  auto previous_sample_interval = impl->_sample_interval;
  impl->_num_channels = spans.size();
  impl->spans = spans;
  impl->_sample_interval = sample_intervals.at(0);
  ready(this);
  impl->_sample_interval = previous_sample_interval;
  impl->_num_channels = temp;
}

static bool is_digital(const std::string &channel) {
  return channel.find("port") != std::string::npos;
}

struct NidaqOutputNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  TaskHandle task_handle;
  NodeGraph *graph;
  std::weak_ptr<AnalogNode> source;
  boost::asio::io_context &io_context;
  std::thread nidaq_thread;
  std::mutex mutex;
  std::condition_variable condition_variable;
  std::vector<std::vector<double>> buffers;
  boost::asio::high_resolution_timer timer;
  std::vector<std::span<const double>> _data;
  size_t _num_channels;
  size_t buffer_size;
  boost::signals2::scoped_connection source_connection;
  std::map<size_t, std::function<void(Node *)>> observers;
  double _sample_rate;
  thalamus::vector<std::chrono::nanoseconds> _sample_intervals;
  size_t counter = 0;
  std::chrono::nanoseconds _time;
  NidaqOutputNode *outer;
  bool running;
  std::vector<std::chrono::steady_clock::time_point> next_write;
  std::vector<std::chrono::steady_clock::time_point> times;
  bool digital = false;
  std::vector<bool> digital_levels;
  std::atomic_bool new_buffers = false;
  std::optional<std::string> current_source;
  std::vector<double> analog_values;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, NidaqOutputNode *_outer)
      : state(_state), task_handle(nullptr), graph(_graph),
        io_context(_io_context), timer(_io_context), outer(_outer),
        running(false) {
    using namespace std::placeholders;
    state_connection = state->recursive_changed.connect(
        std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    this->state->recap(
        std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [&] {});
  }
#ifdef _WIN32
  void nidaq_target() {
    std::vector<double> values;
    std::vector<unsigned char> digital_values;
    while (running) {
      TRACE_EVENT("thalamus", "NidaqOutputNode::on_data");
      std::vector<std::vector<double>> _buffers;
      std::vector<std::chrono::nanoseconds> sample_intervals;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition_variable.wait(
            lock, [&] { return !running || !this->buffers.empty(); });
        if (!running) {
          continue;
        }
        _buffers.swap(this->buffers);
        sample_intervals.swap(_sample_intervals);
        new_buffers = false;
      }
      std::vector<size_t> positions(_buffers.size(), 0);
      times.assign(_buffers.size(), std::chrono::steady_clock::now());
      values.resize(_buffers.size());
      digital_values.resize(_buffers.size());
      digital_levels.assign(_buffers.size(), false);

      auto is_done = [&] {
        auto result = true;
        for (auto i = 0u; i < _buffers.size(); ++i) {
          result = result && positions.at(i) == _buffers.at(i).size();
        }
        return result;
      };

      if (digital) {
        while (!is_done()) {
          auto next_time = std::min_element(times.begin(), times.end());
          auto now = std::chrono::steady_clock::now();
          auto duration = *next_time - now;
          // THALAMUS_LOG(info) << "duration " <<
          // std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
          if (duration > 0s) {
            std::this_thread::sleep_for(duration);
          }
          if (new_buffers) {
            break;
          }
          now = std::chrono::steady_clock::now();

          {
            TRACE_EVENT("thalamus", "NidaqOutputNode::write_signal(analog)");
            auto advanced = false;
            for (auto c = 0u; c < _buffers.size(); ++c) {
              auto &buffer = _buffers.at(c);
              auto &time = times.at(c);
              auto &position = positions.at(c);
              while (time <= now && position < buffer.size()) {
                digital_values.at(c) = buffer.at(position) > 1.6;
                advanced = true;
                ++position;
                time += sample_intervals.at(c);
              }
            }
            if (advanced) {
              auto status = daqmxapi->DAQmxWriteDigitalLines(
                  task_handle, 1, true, -1, DAQmx_Val_GroupByChannel,
                  digital_values.data(), nullptr, nullptr);
              THALAMUS_ASSERT(status >= 0, "DAQmxWriteDigitalLines failed: %d",
                              status);
            }
          }
        }
      } else {
        while (!is_done()) {
          auto next_time = std::min_element(times.begin(), times.end());
          auto now = std::chrono::steady_clock::now();
          auto duration = *next_time - now;
          // THALAMUS_LOG(info) << "duration " <<
          // std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
          if (duration > 0s) {
            std::this_thread::sleep_for(duration);
          }
          if (new_buffers) {
            break;
          }
          now = std::chrono::steady_clock::now();

          {
            TRACE_EVENT("thalamus", "NidaqOutputNode::write_signal(analog)");
            auto advanced = false;
            for (auto c = 0u; c < _buffers.size(); ++c) {
              auto &buffer = _buffers.at(c);
              auto &time = times.at(c);
              auto &position = positions.at(c);
              while (time <= now && position < buffer.size()) {
                values.at(c) = buffer.at(position);
                advanced = true;
                ++position;
                time += sample_intervals.at(c);
              }
            }
            if (advanced) {
              auto status = daqmxapi->DAQmxWriteAnalogF64(
                  task_handle, 1, true, -1, DAQmx_Val_GroupByChannel,
                  values.data(), nullptr, nullptr);
              THALAMUS_ASSERT(status >= 0, "DAQmxWriteAnalogF64 failed: %d",
                              status);
            }
          }
        }
      }
    }
  }
#else
#endif

  ObservableListPtr stims_state;
  void on_change(ObservableCollection *_source, ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::on_change");
    if (_source == stims_state.get()) {
      auto key_int = std::get<long long>(k);
      if (std::holds_alternative<std::string>(v)) {
        stims[int(key_int)] = std::get<std::string>(v);
      } else {
        stims.erase(int(key_int));
      }
      return;
    }
    if (_source != state.get()) {
      return;
    }

    auto key_str = std::get<std::string>(k);

    if (key_str == "Stims") {
      stims_state = std::get<ObservableListPtr>(v);
      stims_state->recap(
          std::bind(&Impl::on_change, this, stims_state.get(), _1, _2, _3));
    } else if (key_str == "Source") {
      std::string source_str = std::get<std::string>(v);
      if (current_source.has_value() && current_source.value() == source_str) {
        return;
      }
      current_source = source_str;

      source_connection.disconnect();

      graph->get_node(source_str, [&](auto node) {
        auto locked_source = node.lock();
        auto analog_node = std::dynamic_pointer_cast<AnalogNode>(locked_source);
        if (!locked_source || analog_node == nullptr) {
          return;
        }
        this->source = std::weak_ptr<AnalogNode>(analog_node);
        source_connection = locked_source->ready.connect(
            std::bind(&Impl::on_data, this, _1, analog_node.get()));
      });
    } else if (key_str == "Running" || key_str == "Channel" ||
               key_str == "Digital") {
      bool new_running = state->at("Running");
      {
        std::unique_lock<std::mutex> lock(mutex);
        running = false;
        condition_variable.notify_all();
      }

      if (nidaq_thread.joinable()) {
        nidaq_thread.join();
      }
      if (task_handle != nullptr) {
        daqmxapi->DAQmxStopTask(task_handle);
        daqmxapi->DAQmxClearTask(task_handle);
        task_handle = nullptr;
        // timer.cancel();
      }

      if (new_running) {
        buffers.clear();
        counter = 0;
        // started = false;
        std::string name = state->at("name");
        std::string channel = state->at("Channel");
        std::string channel_name = name + " channel";
        _num_channels = size_t(NidaqNode::get_num_channels(channel));
        digital = is_digital(channel);
        _sample_rate = -1;
        buffer_size = static_cast<size_t>(16 * _num_channels);
        std::function<void()> reader;
        running = true;
        nidaq_thread = std::thread(std::bind(&Impl::nidaq_target, this));

        auto daq_error = daqmxapi->DAQmxCreateTask(name.c_str(), &task_handle);
        if (daq_error < 0) {
          THALAMUS_LOG(error) << "DAQmxCreateTask failed " << daq_error;
          state->at("Running").assign(false);
          task_handle = nullptr;
          return;
        }

        if (digital) {
          daq_error = daqmxapi->DAQmxCreateDOChan(
              task_handle, channel.c_str(), "", DAQmx_Val_ChanForAllLines);
          if (daq_error < 0) {
            THALAMUS_LOG(error) << "DAQmxCreateDOChan failed " << daq_error;
            state->at("Running").assign(false);
            daqmxapi->DAQmxClearTask(task_handle);
            task_handle = nullptr;
            return;
          }
        } else {
          daq_error = daqmxapi->DAQmxCreateAOVoltageChan(
              task_handle, channel.c_str(), "", -10.0, 10.0, DAQmx_Val_Volts,
              nullptr);
          if (daq_error < 0) {
            THALAMUS_LOG(error)
                << "DAQmxCreateAOVoltageChan failed " << daq_error;
            state->at("Running").assign(false);
            daqmxapi->DAQmxClearTask(task_handle);
            task_handle = nullptr;
            return;
          }
        }

        // daq_error = DAQmxCfgSampClkTiming(task_handle, "", 1000,
        // DAQmx_Val_Rising, DAQmx_Val_ContSamps, max_level);
        // BOOST_ASSERT_MSG(daq_error >= 0, "DAQmxCfgSampClkTiming failed");
        //
        // daq_error = DAQmxRegisterEveryNSamplesEvent(task_handle,
        // DAQmx_Val_Transferred_From_Buffer, buffer_size, 0,
        // Impl::DoneCallbackWrapper, this); BOOST_ASSERT_MSG(daq_error >= 0,
        // "DAQmxRegisterEveryNSamplesEvent failed");

        // if (reader) {
        //   on_timer(reader, polling_interval, boost::system::error_code());
        // }
      } else {
        if (nidaq_thread.joinable()) {
          nidaq_thread.join();
        }
      }
    }
  }

  static int32 CVICALLBACK DoneCallback(TaskHandle taskHandle, int32 status,
                                        void *restart) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::DoneCallback");
    THALAMUS_LOG(info) << "Stim done";
    if (status < 0) {
      THALAMUS_LOG(error) << absl::StrFormat("DAQmx Task failed %d", status);
    }
    daqmxapi->DAQmxStopTask(taskHandle);
    if (restart) {
      daqmxapi->DAQmxStartTask(taskHandle);
    }
    return 0;
  }

  TaskHandle stim_task = nullptr;
  int armed_stim = -1;
  thalamus::map<int, std::string> stims;
  thalamus_grpc::StimResponse
  declare_stim(const thalamus_grpc::StimDeclaration &declaration) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::declare_stim");
    THALAMUS_LOG(info) << "Declaring stim";
    thalamus_grpc::StimResponse response;
    auto &error = *response.mutable_error();
    if (!stims_state) {
      error.set_code(1);
      error.set_message("Not ready, try again later");
      return response;
    }
    auto binary = declaration.SerializeAsString();
    auto encoded = base64_encode(binary);
    for (auto i = stims_state->size(); i <= declaration.id(); ++i) {
      stims_state->at(i).assign(std::monostate());
    }
    stims_state->at(declaration.id()).assign(encoded);
    stims[int(declaration.id())] = encoded;
    return response;
  }

  thalamus_grpc::StimResponse retrieve_stim(int id) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::retrieve_stim");
    THALAMUS_LOG(info) << "Arming stim";
    thalamus_grpc::StimResponse response;
    auto &error = *response.mutable_error();

    if (!stims.contains(id)) {
      error.set_code(2);
      error.set_message("Stim not defined");
      return response;
    }
    auto encoded = stims[id];
    auto binary = base64_decode(encoded);
    thalamus_grpc::StimDeclaration declaration;
    declaration.ParseFromString(binary);
    *response.mutable_declaration() = declaration;
    return response;
  }

  thalamus_grpc::StimResponse arm_stim(int id) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::arm_stim");
    THALAMUS_LOG(info) << "Arming stim";
    thalamus_grpc::StimResponse response;
    auto &error = *response.mutable_error();

    if (stim_task) {
      daqmxapi->DAQmxClearTask(stim_task);
      stim_task = nullptr;
    }
    armed_stim = -1;

    if (!stims.contains(id)) {
      error.set_code(2);
      error.set_message("Stim not defined");
      return response;
    }

    auto encoded = stims[id];
    auto binary = base64_decode(encoded);
    thalamus_grpc::StimDeclaration declaration;
    declaration.ParseFromString(binary);

    auto daq_error = daqmxapi->DAQmxCreateTask("Stim", &stim_task);
    if (daq_error < 0) {
      error.set_code(daq_error);
      error.set_message(
          absl::StrFormat("DAQmxCreateTask failed %d", daq_error));
      THALAMUS_LOG(error) << error.message();
      return response;
    }

    std::vector<std::string> channel_names;
    int num_channels = declaration.data().spans().size();
    int samples_per_channel = declaration.data().spans().empty()
                                  ? 0
                                  : int(declaration.data().spans()[0].end() -
                                        declaration.data().spans()[0].begin());
    for (auto &span : declaration.data().spans()) {
      auto span_size = span.end() - span.begin();
      if (int(span_size) != samples_per_channel) {
        daqmxapi->DAQmxClearTask(stim_task);
        stim_task = nullptr;
        error.set_code(-1);
        error.set_message("All Spans must have the same length");
        THALAMUS_LOG(error) << error.message();
        return response;
      }
      channel_names.push_back(span.name());
    }
    auto physical_channels = absl::StrJoin(channel_names, ",");
    daq_error = daqmxapi->DAQmxCreateAOVoltageChan(
        stim_task, physical_channels.c_str(), "", -10.0, 10.0, DAQmx_Val_Volts,
        nullptr);
    if (daq_error < 0) {
      daqmxapi->DAQmxClearTask(stim_task);
      stim_task = nullptr;
      error.set_code(daq_error);
      error.set_message(
          absl::StrFormat("DAQmxCreateAOVoltageChan failed %d", daq_error));
      THALAMUS_LOG(error) << error.message();
      return response;
    }

    size_t sample_interval = declaration.data().sample_intervals().empty()
                                 ? 0
                                 : declaration.data().sample_intervals()[0];
    for (auto s : declaration.data().sample_intervals()) {
      if (sample_interval != s) {
        daqmxapi->DAQmxClearTask(stim_task);
        stim_task = nullptr;
        error.set_code(-1);
        error.set_message("All sample intervals must be the same");
        THALAMUS_LOG(error) << error.message();
        return response;
      }
    }
    double frequency = 1e9 / double(sample_interval);

    daq_error = daqmxapi->DAQmxCfgSampClkTiming(
        stim_task, "", frequency, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps,
        uint64_t(samples_per_channel));
    if (daq_error < 0) {
      daqmxapi->DAQmxClearTask(stim_task);
      stim_task = nullptr;
      error.set_code(daq_error);
      error.set_message(
          absl::StrFormat("DAQmxCfgSampClkTiming failed %d", daq_error));
      THALAMUS_LOG(error) << error.message();
      return response;
    }

    std::vector<double> data(declaration.data().data().begin(),
                             declaration.data().data().end());
    int offset = 0;
    while (offset < samples_per_channel) {
      int32 count = 0;
      daq_error = daqmxapi->DAQmxWriteAnalogF64(
          stim_task, samples_per_channel - offset, 0, 10.0,
          DAQmx_Val_GroupByChannel, data.data() + num_channels * offset, &count,
          nullptr);
      THALAMUS_LOG(info) << "Wrote " << count << " samples " << offset << " "
                         << samples_per_channel;

      if (daq_error < 0) {
        daqmxapi->DAQmxClearTask(stim_task);
        stim_task = nullptr;
        error.set_code(daq_error);
        error.set_message(
            absl::StrFormat("DAQmxWriteAnalogF64 failed %d", daq_error));
        THALAMUS_LOG(error) << error.message();
        return response;
      }
      offset += count;
    }

    if (!declaration.trigger().empty()) {
      daq_error = daqmxapi->DAQmxCfgDigEdgeStartTrig(
          stim_task, declaration.trigger().c_str(), DAQmx_Val_Rising);
      if (daq_error < 0) {
        daqmxapi->DAQmxClearTask(stim_task);
        stim_task = nullptr;
        error.set_code(daq_error);
        error.set_message(
            absl::StrFormat("DAQmxCfgDigEdgeStartTrig failed %d", daq_error));
        THALAMUS_LOG(error) << error.message();
        return response;
      }
    }

    char *restart = nullptr;
    if (!declaration.trigger().empty()) {
      ++restart;
    }
    daq_error =
        daqmxapi->DAQmxRegisterDoneEvent(stim_task, 0, DoneCallback, restart);
    if (daq_error < 0) {
      daqmxapi->DAQmxClearTask(stim_task);
      stim_task = nullptr;
      error.set_code(daq_error);
      error.set_message(
          absl::StrFormat("DAQmxRegisterDoneEvent failed %d", daq_error));
      THALAMUS_LOG(error) << error.message();
      return response;
    }

    armed_stim = id;

    return response;
  }

  thalamus_grpc::StimResponse trigger_stim(size_t id) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::trigger_stim");
    if (armed_stim != int(id)) {
      thalamus_grpc::StimResponse response = arm_stim(int(id));
      if (response.error().code()) {
        return response;
      }
    }
    thalamus_grpc::StimResponse response;
    auto &error = *response.mutable_error();

    auto daq_error = daqmxapi->DAQmxStartTask(stim_task);
    if (daq_error < 0) {
      error.set_code(daq_error);
      error.set_message(absl::StrFormat("DAQmxStartTask failed %d", daq_error));
      THALAMUS_LOG(error) << error.message();
      return response;
    }
    return response;
  }

  void on_data(Node *, AnalogNode *node) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::on_data");
    if (task_handle == nullptr || !node->has_analog_data()) {
      return;
    }

    _time = node->time();
    {
      std::lock_guard<std::mutex> lock(mutex);
      // buffers.assign(node->num_channels(), std::vector<double>());
      _data.clear();
      buffers.clear();
      _sample_intervals.clear();
      int data_count = 0;
      auto num_channels = std::min(int(_num_channels), node->num_channels());
      for (auto i = 0; i < num_channels; ++i) {
        auto data = node->data(i);
        data_count += data.size();
        buffers.emplace_back(data.begin(), data.end());
        _data.push_back(data);
        _sample_intervals.emplace_back(node->sample_interval(i));
      }
      if (data_count == 0) {
        return;
      }
      new_buffers = true;
    }
    condition_variable.notify_all();
  }
};

NidaqOutputNode::NidaqOutputNode(ObservableDictPtr state,
                                 boost::asio::io_context &io_context,
                                 NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}
NidaqOutputNode::~NidaqOutputNode() {}

std::string NidaqOutputNode::type_name() { return "NIDAQ_OUT (NIDAQMX)"; }

bool NidaqNode::prepare() { return prepare_nidaq(); }
bool NidaqOutputNode::prepare() { return prepare_nidaq(); }

std::future<thalamus_grpc::StimResponse>
NidaqOutputNode::stim(thalamus_grpc::StimRequest &&request) {
  std::promise<thalamus_grpc::StimResponse> response;
  if (request.has_declaration()) {
    response.set_value(impl->declare_stim(request.declaration()));
    //} else if (request.has_arm()) {
    //  response.set_value(impl->arm_stim(request.arm()));
    //} else if (request.has_trigger()) {
    //  response.set_value(impl->trigger_stim(request.trigger()));
    //} else if (request.has_retrieve()) {
    //  response.set_value(impl->retrieve_stim(request.retrieve()));
  }
  return response.get_future();
}

size_t NidaqNode::modalities() const { return infer_modalities<NidaqNode>(); }
size_t NidaqOutputNode::modalities() const {
  return infer_modalities<NidaqOutputNode>();
}
} // namespace thalamus
