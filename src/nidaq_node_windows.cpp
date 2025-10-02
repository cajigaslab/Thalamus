#include <thalamus/tracing.hpp>
#include <base64.hpp>
#include <cstdint>
#include <grpc_impl.hpp>
#include <modalities_util.hpp>
#include <nidaq_node.hpp>
#include <numeric>
#include <regex>
#include <thalamus/thread.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <NIDAQmx.h>
#include <thalamus.pb.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <absl/strings/numbers.h>
#include <absl/strings/str_split.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
  
#ifdef _WIN32
static ::HMODULE library_handle;
#else
static void* library_handle;
#endif

struct DAQmxAPI {
  bool loaded = false;
  decltype(&::DAQmxStartTask) DAQmxStartTask;
  decltype(&::DAQmxStopTask) DAQmxStopTask;
  decltype(&::DAQmxClearTask) DAQmxClearTask;
  decltype(&::DAQmxReadAnalogF64) DAQmxReadAnalogF64;
  decltype(&::DAQmxCreateTask) DAQmxCreateTask;
  decltype(&::DAQmxCreateAIVoltageChan) DAQmxCreateAIVoltageChan;
  decltype(&::DAQmxCreateAICurrentChan) DAQmxCreateAICurrentChan;
  decltype(&::DAQmxCfgSampClkTiming) DAQmxCfgSampClkTiming;
  decltype(&::DAQmxRegisterEveryNSamplesEvent) DAQmxRegisterEveryNSamplesEvent;
  decltype(&::DAQmxWriteDigitalLines) DAQmxWriteDigitalLines;
  decltype(&::DAQmxWriteAnalogScalarF64) DAQmxWriteAnalogScalarF64;
  decltype(&::DAQmxWriteAnalogF64) DAQmxWriteAnalogF64;
  decltype(&::DAQmxCreateDOChan) DAQmxCreateDOChan;
  decltype(&::DAQmxCreateAOVoltageChan) DAQmxCreateAOVoltageChan;
  decltype(&::DAQmxCreateAOCurrentChan) DAQmxCreateAOCurrentChan;
  decltype(&::DAQmxRegisterDoneEvent) DAQmxRegisterDoneEvent;
  decltype(&::DAQmxCfgDigEdgeStartTrig) DAQmxCfgDigEdgeStartTrig;
  decltype(&::DAQmxSetBufInputBufSize) DAQmxSetBufInputBufSize;
  decltype(&::DAQmxGetErrorString) DAQmxGetErrorString;
  decltype(&::DAQmxGetExtendedErrorInfo) DAQmxGetExtendedErrorInfo;
  decltype(&::DAQmxTaskControl) DAQmxTaskControl;
  decltype(&::DAQmxGetSysDevNames) DAQmxGetSysDevNames;
};
static DAQmxAPI *daqmxapi;

#ifdef _WIN32
    template <typename T> T load_function(const std::string &func_name) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#endif
      auto result = reinterpret_cast<T>(
          ::GetProcAddress(library_handle, func_name.c_str()));
#ifdef __clang__
#pragma clang diagnostic pop
#endif
      if (!result) {
        THALAMUS_LOG(info) << "Failed to load " << func_name << ".  "
                           << "NIDAQ disabled";
      }
      return result;
    }
#else
    template <typename T> T load_function(const std::string &func_name) {
      auto result =
          reinterpret_cast<T>(dlsym(library_handle, func_name.c_str()));
      if (!result) {
        THALAMUS_LOG(info) << "Failed to load " << func_name << ".  "
                           << "NIDAQ disabled";
      }
      return result;
    }
#endif

static bool prepare_nidaq() {
  static bool has_run = false;
  if (has_run) {
    return daqmxapi->loaded;
  }
  daqmxapi = new DAQmxAPI();
  has_run = true;
#ifdef _WIN32
      library_handle = LoadLibrary("nicaiu");
#else
      std::string nidaqmx_path = "/usr/lib/x86_64-linux-gnu/libnidaqmx.so.25.5.0";
      library_handle = dlopen(nidaqmx_path.c_str(), RTLD_NOW);
#endif
  if (!library_handle) {
    THALAMUS_LOG(info)
        << "Couldn't find nicaiu.dll.  National Instruments features disabled";
    return false;
  }
  THALAMUS_LOG(info) << "nicaiu.dll found.  Loading DAQmx API";

#ifdef _WIN32
  std::string nidaq_dll_path(256, ' ');
  auto filename_size = uint32_t(nidaq_dll_path.size());
  while (nidaq_dll_path.size() == filename_size) {
    nidaq_dll_path.resize(2 * nidaq_dll_path.size(), ' ');
    filename_size = GetModuleFileNameA(library_handle, nidaq_dll_path.data(),
                                       uint32_t(nidaq_dll_path.size()));
  }
  if (filename_size == 0) {
    THALAMUS_LOG(warning) << "Error while finding nicaiu.dll absolute path";
  } else {
    nidaq_dll_path.resize(filename_size);
    THALAMUS_LOG(info) << "Absolute nicaiu.dll path = " << nidaq_dll_path;
  }
#else
#endif

#define LOAD_FUNC(name)                                                        \
  do {                                                                         \
    daqmxapi->name = load_function<decltype(daqmxapi->name)>(#name);                     \
    if (!daqmxapi->name) {                                                     \
      return false;                                                                  \
    }                                                                          \
  } while (0)
  
  LOAD_FUNC(DAQmxStartTask);
  LOAD_FUNC(DAQmxStopTask);
  LOAD_FUNC(DAQmxClearTask);
  LOAD_FUNC(DAQmxReadAnalogF64);
  LOAD_FUNC(DAQmxCreateTask);
  LOAD_FUNC(DAQmxCreateAIVoltageChan);
  LOAD_FUNC(DAQmxCreateAICurrentChan);
  LOAD_FUNC(DAQmxCfgSampClkTiming);
  LOAD_FUNC(DAQmxRegisterEveryNSamplesEvent);
  LOAD_FUNC(DAQmxWriteDigitalLines);
  LOAD_FUNC(DAQmxWriteAnalogF64);
  LOAD_FUNC(DAQmxWriteAnalogScalarF64);
  LOAD_FUNC(DAQmxCreateDOChan);
  LOAD_FUNC(DAQmxCreateAOVoltageChan);
  LOAD_FUNC(DAQmxCreateAOCurrentChan);
  LOAD_FUNC(DAQmxRegisterDoneEvent);
  LOAD_FUNC(DAQmxCfgDigEdgeStartTrig);
  LOAD_FUNC(DAQmxSetBufInputBufSize);
  LOAD_FUNC(DAQmxGetErrorString);
  LOAD_FUNC(DAQmxGetExtendedErrorInfo);
  LOAD_FUNC(DAQmxTaskControl);
  LOAD_FUNC(DAQmxGetSysDevNames);

  char buffer[1024];
  auto z = daqmxapi->DAQmxGetSysDevNames(buffer, sizeof(buffer));
  THALAMUS_LOG(info) << z << " " << buffer;

  daqmxapi->loaded = true;
  THALAMUS_LOG(info) << "DAQmx API loaded";
  return true;
}

static thalamus::vector<std::string> get_channels(const std::string &channel) {
  if (channel.find(",") != std::string::npos) {
    return absl::StrSplit(channel, ',');
  }
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
    if (task_handle != nullptr) {
      daqmxapi->DAQmxStopTask(task_handle);
      daqmxapi->DAQmxClearTask(task_handle);
      task_handle = nullptr;
    }
  }

  bool check_error(int error, const std::string& function) {
    if(error >= 0) {
      return false;
    }
    auto count = daqmxapi->DAQmxGetErrorString(error, nullptr, 0);
    std::string message(size_t(count), ' ');
    daqmxapi->DAQmxGetErrorString(error, message.data(), uint32_t(message.size()));

    thalamus_grpc::Dialog dialog;
    dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
    dialog.set_title(std::string("NIDAQ Error: ") + function);
    dialog.set_message(message);
    graph->dialog(dialog);
    return true;
  }

  static int32 CVICALLBACK NidaqCallback(TaskHandle task_handle, int32, uInt32,
                                         void *callbackData) {
    auto event_id = get_unique_id();
    TRACE_EVENT("thalamus", "NidaqCallback", perfetto::Flow::ProcessScoped(event_id));
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
    if(daq_error < 0) {
      boost::asio::post(impl->io_context, [this_impl=impl.get(), daq_error] {
        if(this_impl->check_error(daq_error, "DAQmxReadAnalogF64")) {
          daqmxapi->DAQmxClearTask(this_impl->task_handle);
          this_impl->task_handle = nullptr;
          (*this_impl->state)["Running"].assign(false);
        }
      });
      return 0;
    }

    impl->counter += size_t(num_samples);

    boost::asio::post(impl->io_context, [node, moved_buffer = std::move(buffer), event_id,
                                         now]() {
      TRACE_EVENT("thalamus", "NidaqCallback(post)", perfetto::TerminatingFlow::ProcessScoped(event_id));
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
  }

  const std::map<std::string, int32> terminal_configs = {
    {"Default", DAQmx_Val_Cfg_Default},
    {"RSE", DAQmx_Val_RSE},
    {"NRSE", DAQmx_Val_NRSE},
    {"Diff", DAQmx_Val_Diff},
    {"Pseudo Diff", DAQmx_Val_PseudoDiff}
  };
  const std::map<std::string, int32> shunt_resistor_locations = {
    {"Default", DAQmx_Val_Default},
    {"Internal", DAQmx_Val_Internal},
    {"External", DAQmx_Val_External}
  };

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "NidaqNode::on_change");
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
        bool zero_latency = state->at("Zero Latency");

        std::string terminal_config_str = state->at("Terminal Config");
        int32 terminal_config = terminal_configs.at(terminal_config_str);
        std::string channel_type = state->at("Channel Type");

        std::string shunt_resistor_location_str = state->at("Shunt Resistor Location");
        int32 shunt_resistor_location = shunt_resistor_locations.at(shunt_resistor_location_str);
        double shunt_resistor_ohms = state->at("Shunt Resistor Ohms");

        _sample_interval = std::chrono::nanoseconds(size_t(1e9 / sample_rate));

        size_t polling_interval_raw = state->at("Poll Interval");
        std::chrono::milliseconds polling_interval(polling_interval_raw);

        _every_n_samples = zero_latency ? 1 : int(polling_interval / _sample_interval);

        std::string channel_name = name + " channel";
        buffer_size = 20 * size_t(_every_n_samples) * _num_channels;
        std::function<void()> reader;

        auto daq_error = daqmxapi->DAQmxCreateTask(name.c_str(), &task_handle);
        if(check_error(daq_error, "DAQmxCreateTask")) {
          task_handle = nullptr;
          (*state)["Running"].assign(false);
          return;
        }

        if(channel_type == "Voltage") {
          daq_error = daqmxapi->DAQmxCreateAIVoltageChan(
              task_handle, channel.c_str(), channel_name.c_str(),
              terminal_config, -10.0, 10.0, DAQmx_Val_Volts, nullptr);
          if(check_error(daq_error, "DAQmxCreateAIVoltageChan")) {
            daqmxapi->DAQmxClearTask(task_handle);
            task_handle = nullptr;
            (*state)["Running"].assign(false);
            return;
          }
        } else if (channel_type == "Current") {
          daq_error = daqmxapi->DAQmxCreateAICurrentChan(
              task_handle, channel.c_str(), channel_name.c_str(),
              terminal_config, -10.0, 10.0, DAQmx_Val_Amps, shunt_resistor_location, shunt_resistor_ohms, nullptr);
          if(check_error(daq_error, "DAQmxCreateAICurrentChan")) {
            daqmxapi->DAQmxClearTask(task_handle);
            task_handle = nullptr;
            (*state)["Running"].assign(false);
            return;
          }
        } else {
          daqmxapi->DAQmxClearTask(task_handle);
          task_handle = nullptr;
          thalamus_grpc::Dialog dialog;
          dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
          dialog.set_title(std::string("NIDAQ Error"));
          dialog.set_message(std::string("Unexpected channel type: ") + channel_type);
          graph->dialog(dialog);
          return;
        }
        analog_buffer.resize(buffer_size);

        daq_error = daqmxapi->DAQmxCfgSampClkTiming(
            task_handle, nullptr, sample_rate, DAQmx_Val_Rising,
            DAQmx_Val_ContSamps, buffer_size);
        if(check_error(daq_error, "DAQmxCfgSampClkTiming")) {
          daqmxapi->DAQmxClearTask(task_handle);
          task_handle = nullptr;
          (*state)["Running"].assign(false);
          return;
        }

        daq_error = daqmxapi->DAQmxRegisterEveryNSamplesEvent(
            task_handle, DAQmx_Val_Acquired_Into_Buffer,
            uint32_t(_every_n_samples), 0, NidaqCallback,
            new std::weak_ptr<Node>(outer->weak_from_this()));
        if(check_error(daq_error, "DAQmxRegisterEveryNSamplesEvent")) {
          daqmxapi->DAQmxClearTask(task_handle);
          task_handle = nullptr;
          (*state)["Running"].assign(false);
          return;
        }

        daq_error = daqmxapi->DAQmxSetBufInputBufSize(task_handle,
                                                      uint32_t(buffer_size));
        if(check_error(daq_error, "DAQmxSetBufInputBufSize")) {
          daqmxapi->DAQmxClearTask(task_handle);
          task_handle = nullptr;
          (*state)["Running"].assign(false);
          return;
        }

        daq_error = daqmxapi->DAQmxStartTask(task_handle);
        if(check_error(daq_error, "DAQmxStartTask")) {
          daqmxapi->DAQmxClearTask(task_handle);
          task_handle = nullptr;
          (*state)["Running"].assign(false);
          return;
        }
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
  return "NIDAQ (NIDAQMX)";
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
  bool fast_forward = false;
  std::vector<double> last_analog;
  std::vector<unsigned char> last_digital;

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
              if(status < 0) {
                boost::asio::post(io_context, [&] {
                  if(check_error(status)) {
                    (*state)["Running"].assign(false);
                    return;
                  }
                });
                return;
              }
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
              if(status < 0) {
                boost::asio::post(io_context, [&] {
                  if(check_error(status)) {
                    (*state)["Running"].assign(false);
                    return;
                  }
                });
                return;
              }
            }
          }
        }
      }
    }
  }

  bool check_error(int error) {
    if (error >= 0) {
      return false;
    }
    auto count = daqmxapi->DAQmxGetErrorString(error, nullptr, 0);
    std::string message(size_t(count), ' ');
    daqmxapi->DAQmxGetErrorString(error, message.data(), uint32_t(message.size()));

    thalamus_grpc::Dialog dialog;
    dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
    dialog.set_title("NIDAQ Error");
    dialog.set_message(message);
    graph->dialog(dialog);
    return error < 0;
  }

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
        fast_forward = state->at("Fast Foward");
        std::string name = state->at("name");
        std::string channel = state->at("Channel");
        std::string channel_name = name + " channel";
        _num_channels = size_t(NidaqNode::get_num_channels(channel));
        last_digital.resize(_num_channels, false);
        last_analog.resize(_num_channels, 0.0);
        digital = is_digital(channel);
        _sample_rate = -1;
        buffer_size = static_cast<size_t>(16 * _num_channels);
        std::function<void()> reader;
        running = true;
        if(!fast_forward) {
          nidaq_thread = std::thread(std::bind(&Impl::nidaq_target, this));
        }

        auto daq_error = daqmxapi->DAQmxCreateTask(name.c_str(), &task_handle);
        if(check_error(daq_error)) {
          task_handle = nullptr;
          (*state)["Running"].assign(false);
          return;
        }

        if (digital) {
          daq_error = daqmxapi->DAQmxCreateDOChan(
              task_handle, channel.c_str(), "", DAQmx_Val_ChanForAllLines);
        } else {
          daq_error = daqmxapi->DAQmxCreateAOVoltageChan(
              task_handle, channel.c_str(), "", -10.0, 10.0, DAQmx_Val_Volts,
              nullptr);
        }
        if(check_error(daq_error)) {
          daqmxapi->DAQmxClearTask(task_handle);
          task_handle = nullptr;
          (*state)["Running"].assign(false);
          return;
        }

        // daq_error = DAQmxCfgSampClkTiming(task_handle, "", 1000,
        // DAQmx_Val_Rising, DAQmx_Val_ContSamps, max_level);
        // THALAMUS_ASSERT(daq_error >= 0, "DAQmxCfgSampClkTiming failed");
        //
        // daq_error = DAQmxRegisterEveryNSamplesEvent(task_handle,
        // DAQmx_Val_Transferred_From_Buffer, buffer_size, 0,
        // Impl::DoneCallbackWrapper, this); THALAMUS_ASSERT(daq_error >= 0,
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
    auto result = inline_arm_stim(declaration);
    if(result.error().code() == 0) {
      armed_stim = id;
    }
    return response;
  }

  size_t next_stim = 0;

  thalamus_grpc::StimResponse inline_arm_stim(const thalamus_grpc::StimDeclaration& declaration) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::inline_arm_stim");
    thalamus_grpc::StimResponse response;
    auto &error = *response.mutable_error();

    if (stim_task != nullptr) {
      daqmxapi->DAQmxClearTask(stim_task);
      stim_task = nullptr;
    }

    std::string task_name = absl::StrFormat("Stim %d", next_stim++);
    auto daq_error = daqmxapi->DAQmxCreateTask(task_name.c_str(), &stim_task);

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
    if(declaration.data().channel_type() == thalamus_grpc::AnalogResponse_ChannelType_Voltage) {
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
    } else if (declaration.data().channel_type() == thalamus_grpc::AnalogResponse_ChannelType_Current) {
      daq_error = daqmxapi->DAQmxCreateAOCurrentChan(
          stim_task, physical_channels.c_str(), "", -10.0, 10.0, DAQmx_Val_Amps,
          nullptr);
      if (daq_error < 0) {
        daqmxapi->DAQmxClearTask(stim_task);
        stim_task = nullptr;
        error.set_code(daq_error);
        error.set_message(
            absl::StrFormat("DAQmxCreateAOCurrentChan failed %d", daq_error));
        THALAMUS_LOG(error) << error.message();
        return response;
      }
    } else {
      THALAMUS_ASSERT(false, "Unexpected channel type: %d", declaration.data().channel_type());
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

    {
        TRACE_EVENT("thalamus", "DAQmxTaskControl");
        daq_error =
            daqmxapi->DAQmxTaskControl(stim_task, DAQmx_Val_Task_Commit);
        if (daq_error < 0) {
            daqmxapi->DAQmxClearTask(stim_task);
            stim_task = nullptr;
            error.set_code(daq_error);
            error.set_message(
                absl::StrFormat("DAQmxTaskControl failed %d", daq_error));
            THALAMUS_LOG(error) << error.message();
            return response;
        }
    }
    
    armed_stim = std::numeric_limits<int>::max();

    return response;
  }

  thalamus_grpc::StimResponse inline_trigger_stim(const thalamus_grpc::StimDeclaration& declaration) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::inline_trigger_stim");
    auto result = inline_arm_stim(declaration);
    if(result.error().code() == 0) {
      result = trigger_stim(0);
    }
    return result;
  }

  thalamus_grpc::StimResponse trigger_stim(size_t id) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::trigger_stim");
    if (armed_stim != std::numeric_limits<int>::max() && armed_stim != int(id)) {
      thalamus_grpc::StimResponse response = arm_stim(int(id));
      if (response.error().code()) {
        return response;
      }
    }
    thalamus_grpc::StimResponse response;
    auto &error = *response.mutable_error();

    {
      TRACE_EVENT("thalamus", "DAQmxStartTask");
      auto daq_error = daqmxapi->DAQmxStartTask(stim_task);
      if (daq_error < 0) {
        error.set_code(daq_error);
        error.set_message(
            absl::StrFormat("DAQmxStartTask failed %d", daq_error));
        THALAMUS_LOG(error) << error.message();
        return response;
      }
    }
    return response;
  }

  void on_data(Node *, AnalogNode *node) {
    TRACE_EVENT("thalamus", "NidaqOutputNode::on_data");
    if (task_handle == nullptr || !node->has_analog_data()) {
      return;
    }

    _time = node->time();
    auto num_channels = std::min(int(_num_channels), node->num_channels());
    if(fast_forward) {
      for (auto i = 0; i < num_channels; ++i) {
        auto data = node->data(i);
        if(!data.empty()) {
          if(digital) {
            last_digital[size_t(i)] = data.back() > 2.5;
          } else {
            last_analog[size_t(i)] = data.back();
          }
        }
      }
      int status;
      if (digital) {
        TRACE_EVENT("thalamus", "NidaqOutputNode::on_data(write digital)");
        status = daqmxapi->DAQmxWriteDigitalLines(
            task_handle, 1, true, -1, DAQmx_Val_GroupByChannel,
            last_digital.data(), nullptr, nullptr);
      } else {
        TRACE_EVENT("thalamus", "NidaqOutputNode::on_data(write analog)");
        status = daqmxapi->DAQmxWriteAnalogF64(
            task_handle, 1, true, -1, DAQmx_Val_GroupByChannel,
            last_analog.data(), nullptr, nullptr);
      }
      if(check_error(status)) {
        daqmxapi->DAQmxClearTask(task_handle);
        task_handle = nullptr;
        (*state)["Running"].assign(false);
      }
      return;
    } else {
      std::lock_guard<std::mutex> lock(mutex);
      // buffers.assign(node->num_channels(), std::vector<double>());
      _data.clear();
      buffers.clear();
      _sample_intervals.clear();
      int data_count = 0;
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
  switch(request.body_case()) {
    case thalamus_grpc::StimRequest::kDeclaration:
      response.set_value(impl->declare_stim(request.declaration()));
      break;
    case thalamus_grpc::StimRequest::kArm:
      response.set_value(impl->arm_stim(int(request.arm())));
      break;
    case thalamus_grpc::StimRequest::kInlineArm:
      response.set_value(impl->inline_arm_stim(request.inline_arm()));
      break;
    case thalamus_grpc::StimRequest::kTrigger:
      response.set_value(impl->trigger_stim(request.trigger()));
      break;
    case thalamus_grpc::StimRequest::kInlineTrigger:
      response.set_value(impl->inline_trigger_stim(request.inline_trigger()));
      break;
    case thalamus_grpc::StimRequest::kRetrieve:
      response.set_value(impl->retrieve_stim(int(request.retrieve())));
      break;
    case thalamus_grpc::StimRequest::kNode:
    case thalamus_grpc::StimRequest::BODY_NOT_SET:
      THALAMUS_ASSERT(false, "Unexpected Stim request");
      break;
  }
  return response.get_future();
}

size_t NidaqNode::modalities() const { return infer_modalities<NidaqNode>(); }
size_t NidaqOutputNode::modalities() const {
  return infer_modalities<NidaqOutputNode>();
}
} // namespace thalamus
