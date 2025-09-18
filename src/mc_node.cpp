#include <thalamus/tracing.hpp>
#include <base64.hpp>
#include <cstdint>
#include <grpc_impl.hpp>
#include <modalities_util.hpp>
#include <mc_node.hpp>
#include <numeric>
#include <regex>
#include <thalamus/thread.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <cbw.h>
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

struct CbwAPI {
  bool loaded = false;
  decltype(&::cbDeclareRevision) cbDeclareRevision;
  decltype(&::cbErrHandling) cbErrHandling;
  decltype(&::cbGetErrMsg) cbGetErrMsg;
  decltype(&::cbDConfigPort) cbDConfigPort;
  decltype(&::cbDIn) cbDIn;
  decltype(&::cbDOut) cbDOut;
};
static CbwAPI *cbwapi;

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
                           << "MC disabled";
      }
      return result;
    }
#else
    template <typename T> T load_function(const std::string &func_name) {
      auto result =
          reinterpret_cast<T>(dlsym(library_handle, func_name.c_str()));
      if (!result) {
        THALAMUS_LOG(info) << "Failed to load " << func_name << ".  "
                           << "MC disabled";
      }
      return result;
    }
#endif

static bool prepare_mc() {
  static bool has_run = false;
  if (has_run) {
    return cbwapi->loaded;
  }
  cbwapi = new CbwAPI();
  has_run = true;
#ifdef _WIN32
      library_handle = LoadLibrary("cbw64");
#else
      std::string cbw_path = "/usr/lib/x86_64-linux-gnu/cbw.so";
      library_handle = dlopen(cbw_path.c_str(), RTLD_NOW);
#endif
  if (!library_handle) {
    THALAMUS_LOG(info)
        << "Couldn't find cbw64.dll.  National Instruments features disabled";
    return false;
  }
  THALAMUS_LOG(info) << "cbw64.dll found.  Loading DAQmx API";

#ifdef _WIN32
  std::string cbw_dll_path(256, ' ');
  auto filename_size = uint32_t(cbw_dll_path.size());
  while (cbw_dll_path.size() == filename_size) {
    cbw_dll_path.resize(2 * cbw_dll_path.size(), ' ');
    filename_size = GetModuleFileNameA(library_handle, cbw_dll_path.data(),
                                       uint32_t(cbw_dll_path.size()));
  }
  if (filename_size == 0) {
    THALAMUS_LOG(warning) << "Error while finding cbw64.dll absolute path";
  } else {
    cbw_dll_path.resize(filename_size);
    THALAMUS_LOG(info) << "Absolute cbw64.dll path = " << cbw_dll_path;
  }
#else
#endif

#define LOAD_FUNC(name)                                                        \
  do {                                                                         \
    cbwapi->name = load_function<decltype(cbwapi->name)>(#name);                     \
    if (!cbwapi->name) {                                                     \
      return false;                                                                  \
    }                                                                          \
  } while (0)
  
  LOAD_FUNC(cbDeclareRevision);
  LOAD_FUNC(cbErrHandling);
  LOAD_FUNC(cbGetErrMsg);
  LOAD_FUNC(cbDConfigPort);
  LOAD_FUNC(cbDIn);
  LOAD_FUNC(cbDOut);
  
	float	RevLevel = static_cast<float>(CURRENTREVNUM);
	auto ULStat = cbwapi->cbDeclareRevision(&RevLevel);
  if(ULStat != NOERRORS) {
    THALAMUS_LOG(warning) << "cbDeclareRevision failed, disabling Measurement Computing";
    return false;
  }
	ULStat = cbwapi->cbErrHandling(DONTPRINT, DONTSTOP);
  if(ULStat != NOERRORS) {
    THALAMUS_LOG(warning) << "cbErrHandling failed, disabling Measurement Computing";
    return false;
  }

  cbwapi->loaded = true;
  THALAMUS_LOG(info) << "DAQmx API loaded";
  return true;
}

struct McNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  boost::asio::io_context &io_context;
  boost::asio::high_resolution_timer timer;
  size_t analog_buffer_position;
  std::vector<double> analog_buffer;
  std::vector<double> output_buffer;
  std::vector<std::span<double const>> spans;
  size_t _num_channels;
  size_t buffer_size;
  std::chrono::nanoseconds _sample_interval = 32ms;
  size_t counter = 0;
  std::chrono::nanoseconds _time = 0ns;
  std::atomic_bool busy;
  NodeGraph *graph;
  McNode *outer;
  bool is_running;
  int port;
  std::jthread poll_thread;
  thalamus::vector<std::string> recommended_names;
  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, McNode *_outer)
      : state(_state), io_context(_io_context),
        timer(_io_context), analog_buffer_position(0), busy(false),
        graph(_graph), outer(_outer), is_running(false) {
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  ~Impl() {
    (*state)["Running"].assign(false, [] {});
    if(poll_thread.joinable()) {
      poll_thread.request_stop();
      poll_thread.join();
    }
  }

  bool check_error(int error, const std::string& function) {
    if(error == NOERROR) {
      return false;
    }

    char buffer[ERRSTRLEN];
    cbwapi->cbGetErrMsg(error, buffer);
    std::string message = buffer;

    thalamus_grpc::Dialog dialog;
    dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
    dialog.set_title(std::string("Measurement Compuring Error: ") + function);
    dialog.set_message(message);
    graph->dialog(dialog);
    return true;
  }

  void loop(std::stop_token stoken) {
    auto handle_error = [this] (int status,std::string_view function) {
      if(status != NOERROR) {
        boost::asio::post(io_context, [this,status,f=std::string(function)] {
          if(check_error(status, f)) {
            return;
          }
        });
        return true;
      }
      return false;
    };

    auto status = cbwapi->cbDConfigPort(0, FIRSTPORTA, DIGITALIN);
    if(handle_error(status, "cbDConfigPort(FIRSTPORTA, DIGITALIN)")) {
      return;
    }
    status = cbwapi->cbDConfigPort(0, FIRSTPORTB, DIGITALOUT);
    if(handle_error(status, "cbDConfigPort(FIRSTPORTB, DIGITALOUT)")) {
      return;
    }

    auto pub_count = 32/ std::chrono::duration_cast<std::chrono::milliseconds>(_sample_interval).count();
    std::vector<uint16_t> samples;
    samples.reserve(pub_count);
    auto next_sample = std::chrono::steady_clock::now();
    auto high = false;
    auto last_log = std::chrono::steady_clock::now();
    auto read_time = 0ns;
    while(!stoken.stop_requested()) {
      uint16_t value;
      auto read_start = std::chrono::steady_clock::now();
      status = cbwapi->cbDIn(0, FIRSTPORTA, &value);
      auto read_end = std::chrono::steady_clock::now();
      read_time += read_end - read_start;
      if (read_end - last_log >= 1s) {
          auto read_milli = std::chrono::duration_cast<std::chrono::milliseconds>(read_time).count();
          auto log_milli = std::chrono::duration_cast<std::chrono::milliseconds>(read_end - last_log).count();

          THALAMUS_LOG(info) << "cbDIn " << log_milli << " " << read_milli << " " << double(read_milli)/double(log_milli);
          read_time = 0ns;
          last_log = read_end;
      }
      if(handle_error(status, "cbDIn")) {
        return;
      }
      samples.push_back(value);
      std::chrono::milliseconds sample_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(next_sample.time_since_epoch());
      auto new_high = ((sample_epoch.count () % 1000) < 300) ? true : false;
      if(new_high != high) {
        high = new_high;
        status = cbwapi->cbDOut(0, FIRSTPORTB, high ? 255 : 0);
        if(handle_error(status, "cbDOut")) {
          return;
        }
      }

      auto now = std::chrono::steady_clock::now();
      if(samples.size() >= pub_count) {
        boost::asio::post(io_context, [this,samples,now] {
          output_buffer.clear();
          spans.clear();
          for(auto i = 0;i < 8;++i) {
            auto mask = 1 << i;
            for(auto sample : samples) {
              output_buffer.push_back((sample & mask) ? 5 : 0);
            }
          }
          for(auto i = 0;i < 8;++i) {
            spans.emplace_back(output_buffer.begin() + i*int64_t(samples.size()),
                               output_buffer.begin() + (i+1)*int64_t(samples.size()));
          }
          _time = now.time_since_epoch();
          outer->ready(outer);
        });
        samples.clear();
      }

      next_sample += _sample_interval;
      auto duration = next_sample - now;
      if(duration > 0s) {
        std::this_thread::sleep_for(duration);
      }
    }
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "McNode::on_change");
    auto key_str = std::get<std::string>(k);
    if (key_str == "Port") {
      //std::string port = state->at("Port");
      //if(port == "A") {
      //  port = FIRSTPORTA;
      //} else if (port == "B") {
      //  port = FIRSTPORTB;
      //} else if (port == "C") {
      //  port = FIRSTPORTC;
      //}
    } else if (key_str == "Running") {
      auto current_is_running = std::get<bool>(v);
      if(poll_thread.joinable()) {
        poll_thread.request_stop();
        poll_thread.join();
      }
      if (current_is_running) {
        poll_thread = std::jthread([this] (auto token) {
          loop(token);
        });
        outer->channels_changed(outer);
      }
    }
  }
};

McNode::McNode(ObservableDictPtr state,
                     boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

McNode::~McNode() {}

std::string McNode::type_name() {
  return "MC";
}

std::span<const double> McNode::data(int channel) const {
  return impl->spans.at(size_t(channel));
}

int McNode::num_channels() const { return 8; }

std::chrono::nanoseconds McNode::sample_interval(int) const {
  return impl->_sample_interval;
}

std::chrono::nanoseconds McNode::time() const { return impl->_time; }

std::string_view McNode::name(int channel) const {
  switch(channel) {
    case 0:
      return "0";
    case 1:
      return "1";
    case 2:
      return "2";
    case 3:
      return "3";
    case 4:
      return "4";
    case 5:
      return "5";
    case 6:
      return "6";
    case 7:
      return "7";
    default:
      return "ERR";
  }
}

void McNode::inject(
    const thalamus::vector<std::span<double const>> &,
    const thalamus::vector<std::chrono::nanoseconds> &,
    const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}
/*
struct McOutputNode::Impl {
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
  McOutputNode *outer;
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
       NodeGraph *_graph, McOutputNode *_outer)
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
      TRACE_EVENT("thalamus", "McOutputNode::on_data");
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
            TRACE_EVENT("thalamus", "McOutputNode::write_signal(analog)");
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
              auto status = cbwapi->DAQmxWriteDigitalLines(
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
            TRACE_EVENT("thalamus", "McOutputNode::write_signal(analog)");
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
              auto status = cbwapi->DAQmxWriteAnalogF64(
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
    auto count = cbwapi->DAQmxGetErrorString(error, nullptr, 0);
    std::string message(size_t(count), ' ');
    cbwapi->DAQmxGetErrorString(error, message.data(), uint32_t(message.size()));

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
    TRACE_EVENT("thalamus", "McOutputNode::on_change");
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
        cbwapi->DAQmxStopTask(task_handle);
        cbwapi->DAQmxClearTask(task_handle);
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
        _num_channels = size_t(McNode::get_num_channels(channel));
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

        auto daq_error = cbwapi->DAQmxCreateTask(name.c_str(), &task_handle);
        if(check_error(daq_error)) {
          task_handle = nullptr;
          (*state)["Running"].assign(false);
          return;
        }

        if (digital) {
          daq_error = cbwapi->DAQmxCreateDOChan(
              task_handle, channel.c_str(), "", DAQmx_Val_ChanForAllLines);
        } else {
          daq_error = cbwapi->DAQmxCreateAOVoltageChan(
              task_handle, channel.c_str(), "", -10.0, 10.0, DAQmx_Val_Volts,
              nullptr);
        }
        if(check_error(daq_error)) {
          cbwapi->DAQmxClearTask(task_handle);
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
    TRACE_EVENT("thalamus", "McOutputNode::DoneCallback");
    THALAMUS_LOG(info) << "Stim done";
    if (status < 0) {
      THALAMUS_LOG(error) << absl::StrFormat("DAQmx Task failed %d", status);
    }
    cbwapi->DAQmxStopTask(taskHandle);
    if (restart) {
      cbwapi->DAQmxStartTask(taskHandle);
    }
    return 0;
  }

  TaskHandle stim_task = nullptr;
  int armed_stim = -1;
  thalamus::map<int, std::string> stims;
  thalamus_grpc::StimResponse
  declare_stim(const thalamus_grpc::StimDeclaration &declaration) {
    TRACE_EVENT("thalamus", "McOutputNode::declare_stim");
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
    TRACE_EVENT("thalamus", "McOutputNode::retrieve_stim");
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
    TRACE_EVENT("thalamus", "McOutputNode::arm_stim");
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
    TRACE_EVENT("thalamus", "McOutputNode::inline_arm_stim");
    thalamus_grpc::StimResponse response;
    auto &error = *response.mutable_error();

    if (stim_task != nullptr) {
      cbwapi->DAQmxClearTask(stim_task);
      stim_task = nullptr;
    }

    std::string task_name = absl::StrFormat("Stim %d", next_stim++);
    auto daq_error = cbwapi->DAQmxCreateTask(task_name.c_str(), &stim_task);

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
        cbwapi->DAQmxClearTask(stim_task);
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
      daq_error = cbwapi->DAQmxCreateAOVoltageChan(
          stim_task, physical_channels.c_str(), "", -10.0, 10.0, DAQmx_Val_Volts,
          nullptr);
      if (daq_error < 0) {
        cbwapi->DAQmxClearTask(stim_task);
        stim_task = nullptr;
        error.set_code(daq_error);
        error.set_message(
            absl::StrFormat("DAQmxCreateAOVoltageChan failed %d", daq_error));
        THALAMUS_LOG(error) << error.message();
        return response;
      }
    } else if (declaration.data().channel_type() == thalamus_grpc::AnalogResponse_ChannelType_Current) {
      daq_error = cbwapi->DAQmxCreateAOCurrentChan(
          stim_task, physical_channels.c_str(), "", -10.0, 10.0, DAQmx_Val_Amps,
          nullptr);
      if (daq_error < 0) {
        cbwapi->DAQmxClearTask(stim_task);
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
        cbwapi->DAQmxClearTask(stim_task);
        stim_task = nullptr;
        error.set_code(-1);
        error.set_message("All sample intervals must be the same");
        THALAMUS_LOG(error) << error.message();
        return response;
      }
    }
    double frequency = 1e9 / double(sample_interval);

    daq_error = cbwapi->DAQmxCfgSampClkTiming(
        stim_task, "", frequency, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps,
        uint64_t(samples_per_channel));
    if (daq_error < 0) {
      cbwapi->DAQmxClearTask(stim_task);
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
      daq_error = cbwapi->DAQmxWriteAnalogF64(
          stim_task, samples_per_channel - offset, 0, 10.0,
          DAQmx_Val_GroupByChannel, data.data() + num_channels * offset, &count,
          nullptr);
      THALAMUS_LOG(info) << "Wrote " << count << " samples " << offset << " "
                         << samples_per_channel;

      if (daq_error < 0) {
        cbwapi->DAQmxClearTask(stim_task);
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
      daq_error = cbwapi->DAQmxCfgDigEdgeStartTrig(
          stim_task, declaration.trigger().c_str(), DAQmx_Val_Rising);
      if (daq_error < 0) {
        cbwapi->DAQmxClearTask(stim_task);
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
        cbwapi->DAQmxRegisterDoneEvent(stim_task, 0, DoneCallback, restart);
    if (daq_error < 0) {
      cbwapi->DAQmxClearTask(stim_task);
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
            cbwapi->DAQmxTaskControl(stim_task, DAQmx_Val_Task_Commit);
        if (daq_error < 0) {
            cbwapi->DAQmxClearTask(stim_task);
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
    TRACE_EVENT("thalamus", "McOutputNode::inline_trigger_stim");
    auto result = inline_arm_stim(declaration);
    if(result.error().code() == 0) {
      result = trigger_stim(0);
    }
    return result;
  }

  thalamus_grpc::StimResponse trigger_stim(size_t id) {
    TRACE_EVENT("thalamus", "McOutputNode::trigger_stim");
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
      auto daq_error = cbwapi->DAQmxStartTask(stim_task);
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
    TRACE_EVENT("thalamus", "McOutputNode::on_data");
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
        TRACE_EVENT("thalamus", "McOutputNode::on_data(write digital)");
        status = cbwapi->DAQmxWriteDigitalLines(
            task_handle, 1, true, -1, DAQmx_Val_GroupByChannel,
            last_digital.data(), nullptr, nullptr);
      } else {
        TRACE_EVENT("thalamus", "McOutputNode::on_data(write analog)");
        status = cbwapi->DAQmxWriteAnalogF64(
            task_handle, 1, true, -1, DAQmx_Val_GroupByChannel,
            last_analog.data(), nullptr, nullptr);
      }
      if(check_error(status)) {
        cbwapi->DAQmxClearTask(task_handle);
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

McOutputNode::McOutputNode(ObservableDictPtr state,
                                 boost::asio::io_context &io_context,
                                 NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}
McOutputNode::~McOutputNode() {}

std::string McOutputNode::type_name() { return "NIDAQ_OUT (NIDAQMX)"; }

bool McOutputNode::prepare() { return prepare_nidaq(); }

std::future<thalamus_grpc::StimResponse>
McOutputNode::stim(thalamus_grpc::StimRequest &&request) {
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

size_t McOutputNode::modalities() const {
  return infer_modalities<McOutputNode>();
}
  */
bool McNode::prepare() { return prepare_mc(); }
size_t McNode::modalities() const { return infer_modalities<McNode>(); }
} // namespace thalamus
