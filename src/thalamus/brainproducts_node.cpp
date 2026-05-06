#include <thalamus/tracing.hpp>
#include <thalamus/brainproducts_node.hpp>
#include <cstdint>
#include <thalamus/modalities_util.hpp>
#include <random>
#include <thalamus/async.hpp>
#include <thalamus/thread.hpp>

#include <AmplifierSDK.h>

using namespace thalamus;
using namespace std::placeholders;

static ::HMODULE amplifier_dll_handle;

struct AmplifierSDK {
  bool loaded = false;
  decltype(&::SetAmplifierFamily) SetAmplifierFamily;
  decltype(&::EnumerateDevices) EnumerateDevices;
  decltype(&::OpenDevice) OpenDevice;
  decltype(&::CloseDevice) CloseDevice;
  decltype(&::GetProperty) GetProperty;
  decltype(&::SetProperty) SetProperty;
  decltype(&::GetInfo) GetInfo;
  decltype(&::StartAcquisition) StartAcquisition;
  decltype(&::StopAcquisition) StopAcquisition;
  decltype(&::GetData) GetData;
};
static AmplifierSDK *amplifier_sdk;

static bool prepare_amplifier() {
  static bool has_run = false;
  if (has_run) {
    return amplifier_sdk->loaded;
  }
  amplifier_sdk = new AmplifierSDK();
  has_run = true;
  amplifier_dll_handle = LoadLibrary("AmplifierSDK");
  if (!amplifier_dll_handle) {
    THALAMUS_LOG(info)
        << "Couldn't find AmplifierSDK.dll.  Brain Products features disabled";
    return false;
  }
  THALAMUS_LOG(info) << "AmplifierSDK.dll found.  Loading BrainVision Amplifier API";

  std::string amplifier_dll_path(256, ' ');
  DWORD filename_size = uint32_t(amplifier_dll_path.size());
  while (amplifier_dll_path.size() == filename_size) {
    amplifier_dll_path.resize(2 * amplifier_dll_path.size(), ' ');
    filename_size = GetModuleFileNameA(amplifier_dll_handle, amplifier_dll_path.data(),
                                       uint32_t(amplifier_dll_path.size()));
  }
  if (filename_size == 0) {
    THALAMUS_LOG(warning) << "Error while finding AmplifierSDK.dll absolute path";
  } else {
    amplifier_dll_path.resize(filename_size);
    THALAMUS_LOG(info) << "Absolute AmplifierSDK.dll path = " << amplifier_dll_path;
  }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#endif
  amplifier_sdk->SetAmplifierFamily = reinterpret_cast<decltype(&SetAmplifierFamily)>(
      ::GetProcAddress(amplifier_dll_handle, "SetAmplifierFamily"));
  if (!amplifier_sdk->SetAmplifierFamily) {
    THALAMUS_LOG(info)
        << "Failed to load SetAmplifierFamily.  Brain Products features disabled";
    return false;
  }
  
  amplifier_sdk->EnumerateDevices = reinterpret_cast<decltype(&EnumerateDevices)>(
      ::GetProcAddress(amplifier_dll_handle, "EnumerateDevices"));
  if (!amplifier_sdk->EnumerateDevices) {
    THALAMUS_LOG(info)
        << "Failed to load EnumerateDevices.  Brain Products features disabled";
    return false;
  }
  
  amplifier_sdk->OpenDevice = reinterpret_cast<decltype(&OpenDevice)>(
      ::GetProcAddress(amplifier_dll_handle, "OpenDevice"));
  if (!amplifier_sdk->OpenDevice) {
    THALAMUS_LOG(info)
        << "Failed to load OpenDevice.  Brain Products features disabled";
    return false;
  }

  amplifier_sdk->CloseDevice = reinterpret_cast<decltype(&CloseDevice)>(
      ::GetProcAddress(amplifier_dll_handle, "CloseDevice"));
  if (!amplifier_sdk->CloseDevice) {
      THALAMUS_LOG(info)
          << "Failed to load CloseDevice.  Brain Products features disabled";
      return false;
  }
  
  amplifier_sdk->GetProperty = reinterpret_cast<decltype(&GetProperty)>(
      ::GetProcAddress(amplifier_dll_handle, "GetProperty"));
  if (!amplifier_sdk->GetProperty) {
    THALAMUS_LOG(info)
        << "Failed to load GetProperty.  Brain Products features disabled";
    return false;
  }

  amplifier_sdk->SetProperty = reinterpret_cast<decltype(&SetProperty)>(
      ::GetProcAddress(amplifier_dll_handle, "SetProperty"));
  if (!amplifier_sdk->SetProperty) {
      THALAMUS_LOG(info)
          << "Failed to load SetProperty.  Brain Products features disabled";
      return false;
  }
  
  amplifier_sdk->GetInfo = reinterpret_cast<decltype(&GetInfo)>(
      ::GetProcAddress(amplifier_dll_handle, "GetInfo"));
  if (!amplifier_sdk->GetInfo) {
    THALAMUS_LOG(info)
        << "Failed to load GetInfo.  Brain Products features disabled";
    return false;
  }
  
  amplifier_sdk->StartAcquisition = reinterpret_cast<decltype(&StartAcquisition)>(
      ::GetProcAddress(amplifier_dll_handle, "StartAcquisition"));
  if (!amplifier_sdk->StartAcquisition) {
    THALAMUS_LOG(info)
        << "Failed to load StartAcquisition.  Brain Products features disabled";
    return false;
  }
  
  amplifier_sdk->StopAcquisition = reinterpret_cast<decltype(&StopAcquisition)>(
      ::GetProcAddress(amplifier_dll_handle, "StopAcquisition"));
  if (!amplifier_sdk->StopAcquisition) {
    THALAMUS_LOG(info)
        << "Failed to load StopAcquisition.  Brain Products features disabled";
    return false;
  }
  
  amplifier_sdk->GetData = reinterpret_cast<decltype(&GetData)>(
      ::GetProcAddress(amplifier_dll_handle, "GetData"));
  if (!amplifier_sdk->GetData) {
    THALAMUS_LOG(info)
        << "Failed to load GetData.  Brain Products features disabled";
    return false;
  }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  amplifier_sdk->loaded = true;
  THALAMUS_LOG(info) << "BrainVision Amplifier API loaded";
  return true;
}

struct BrainProductsNode::Impl {
  struct Channel {
    double resolution;
    int datatype;
  };

  template <typename T> void extract(std::vector<T>& output_data_ref, std::vector<std::span<const T>>& spans_ref, size_t num_samples, std::vector<Channel>& channels,
                                            unsigned char* buffer_bytes, int read_count) {
    auto i = 0ull;
    auto offset = 0;
    output_data_ref.resize(channels.size() * num_samples);
    while(offset < read_count) {
      offset += 8;
      auto j = 0ull;
      for(auto& channel : channels) {
        switch (channel.datatype)
        {
        case DT_INT16:
          output_data_ref[j*num_samples + i] = T(*reinterpret_cast<short*>(buffer_bytes + offset));
          offset += 2;
          break;
        case DT_UINT16:
          output_data_ref[j*num_samples + i] = T(*reinterpret_cast<unsigned short*>(buffer_bytes + offset));
          offset += 2;
          break;
        case DT_INT32:
          output_data_ref[j*num_samples + i] = T(*reinterpret_cast<int*>(buffer_bytes + offset));
          offset += 4;
          break;
        case DT_UINT32:
          output_data_ref[j*num_samples + i] = T(*reinterpret_cast<unsigned int*>(buffer_bytes + offset));
          offset += 4;
          break;
        case DT_FLOAT32:
          output_data_ref[j*num_samples + i] = T(*reinterpret_cast<float*>(buffer_bytes + offset));
          offset += 4;
          break;
        case DT_INT64:
          output_data_ref[j*num_samples + i] = T(*reinterpret_cast<int64_t*>(buffer_bytes + offset));
          offset += 8;
          break;
        case DT_UINT64:
          output_data_ref[j*num_samples + i] = T(*reinterpret_cast<uint64_t*>(buffer_bytes + offset));
          offset += 8;
          break;
        case DT_FLOAT64:
          output_data_ref[j*num_samples + i] = T(*reinterpret_cast<double*>(buffer_bytes + offset));
          offset += 8;
          break;
        default:
          on_error("BrainVision Amplifier Error", "Unexpected channel data type");
          return;
        }
        ++j;
      }
      ++i;
    }

    for(i = 0;i < channels.size();++i) {
      spans_ref[i] = std::span<const T>(output_data_ref.begin() + int64_t(i*num_samples), output_data_ref.begin() + int64_t((i+1)*num_samples));
    }
  }

  ObservableDictPtr state;
  ObservableList *nodes;
  boost::signals2::scoped_connection state_connection;
  NodeGraph *graph;
  std::weak_ptr<Node> source;
  boost::asio::io_context &io_context;
  boost::asio::high_resolution_timer timer;
  std::vector<std::vector<double>> buffers;
  thalamus::vector<std::string> names;
  thalamus::vector<std::string_view> name_views;
  std::vector<std::string> recommended_names;
  size_t _num_channels;
  size_t buffer_size;
  size_t source_observer_id;
  std::map<size_t, std::function<void(Node *)>> observers;
  size_t counter = 0;
  double current = 0;
  std::random_device random_device;
  std::mt19937 random_range;
  std::uniform_int_distribution<std::mt19937::result_type> random_distribution;
  double frequency;
  double amplitude;
  double phase;
  size_t poll_interval;
  bool is_running;
  std::chrono::nanoseconds _sample_interval;
  std::chrono::nanoseconds _time;
  std::chrono::steady_clock::time_point last_time;
  std::chrono::steady_clock::time_point _start_time;
  BrainProductsNode *outer;
  std::optional<bool> is_int_data;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, BrainProductsNode *_outer)
      : state(_state), nodes(static_cast<ObservableList *>(state->parent)),
        graph(_graph), io_context(_io_context), timer(io_context),
        recommended_names(1, "0"), random_range(random_device()),
        random_distribution(0, 1), outer(_outer) {
    state_connection = state->recursive_changed.connect(
        std::bind(&Impl::on_change, this, _1, _2, _3, _4));

    this->state->recap(
        std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }
  ~Impl() {
    if(acquisition_thread.joinable()) {
      acquisition_thread.request_stop();
      acquisition_thread.join();
    }
    (*state)["Running"].assign(false, [&] {});
  }

  void on_error(const std::string& title, const std::string& message) {
      boost::asio::post(io_context, [this, title, message] {
        thalamus_grpc::Dialog dialog;
        dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
        dialog.set_title(title);
        dialog.set_message(message);
        graph->dialog(dialog);
        (*state)["Running"].assign(false);
      });
  }

  int datatype_to_bytes(int nDataType) {
    switch (nDataType)
    {
    case DT_INT16:
    case DT_UINT16:
      return 2;
    case DT_INT32:
    case DT_UINT32:
    case DT_FLOAT32:
      return 4;
    case DT_INT64:
    case DT_UINT64:
    case DT_FLOAT64:
      return 8;
    default:
      return -1;
    }
  }

  std::mutex mutex;
  std::vector<std::string> channel_names;
  std::vector<std::chrono::nanoseconds> sample_intervals;
  std::vector<std::span<const double>> spans;
  std::vector<double> output_data;
  std::vector<std::span<const int>> int_spans;
  std::vector<int> int_output_data;
  std::vector<double> scales;
  bool busy = false;
  std::condition_variable cond;

  void acquisition_target(std::stop_token stop_token) {
    set_current_thread_name("Brain Products");
    auto status = amplifier_sdk->SetAmplifierFamily(AmplifierFamily::eActiChampFamily);
    if (status != AMP_OK) {
      on_error("BrainVision Amplifier Error", "Failed to set amplifier family");
      return;
    }
    char hwi[4] = "USB";
    auto device_count = amplifier_sdk->EnumerateDevices(hwi, sizeof(hwi), "", 0);
    if (device_count == 0) {
      on_error("BrainVision Amplifier Error", "No USB amplifier found");
      return;
    }

    HANDLE amplifier;
    status = amplifier_sdk->OpenDevice(0, &amplifier);
    if (status != AMP_OK) {
      on_error("BrainVision Amplifier Error", "Failed to open amplifier");
      return;
    }

    Finally f([&] {
      status = amplifier_sdk->CloseDevice(amplifier);
      if (status != AMP_OK) {
        on_error("BrainVision Amplifier Error", "Failed to start acquisition");
        return;
      }
    });

    char cValue[200];
    status = amplifier_sdk->GetProperty(amplifier, PG_DEVICE, 0, DPROP_CHR_SerialNumber, cValue, sizeof(cValue));
    std::string serial_number = cValue;
    if (status != AMP_OK) {
      on_error("BrainVision Amplifier Error", "Failed to read serial number");
      return;
    }

    status = amplifier_sdk->GetProperty(amplifier, PG_DEVICE, 0, DPROP_CHR_Type, cValue, sizeof(cValue));
    std::string amp_type = cValue;
    if (status != AMP_OK) {
      on_error("BrainVision Amplifier Error", "Failed to read amp type");
      return;
    }

    VersionNumber apiVersion, libVersion;
    amplifier_sdk->GetInfo(InfoType::eAPIVersion, static_cast<void*>(&apiVersion), sizeof(t_VersionNumber));
    amplifier_sdk->GetInfo(InfoType::eLIBVersion, static_cast<void*>(&libVersion), sizeof(t_VersionNumber));
    THALAMUS_LOG(info) << "API Version " <<
      apiVersion.Major << "." <<
      apiVersion.Minor << "." <<
      apiVersion.Build << "." <<
      apiVersion.Revision;

    THALAMUS_LOG(info) << "Library Version " <<
      libVersion.Major << "." <<
      libVersion.Minor << "." <<
      libVersion.Build << "." <<
      libVersion.Revision;

    float fBaseSamplingRate, fSubSampleDivisor;
    status = amplifier_sdk->GetProperty(amplifier, PG_DEVICE, 0, DPROP_F32_BaseSampleRate, &fBaseSamplingRate, sizeof(fBaseSamplingRate));
    if (status != AMP_OK) {
      on_error("BrainVision Amplifier Error", "Failed to read base sample rate");
      return;
    }
    status = amplifier_sdk->GetProperty(amplifier, PG_DEVICE, 0, DPROP_F32_SubSampleDivisor, &fSubSampleDivisor, sizeof(fSubSampleDivisor));
    if (status != AMP_OK) {
      on_error("BrainVision Amplifier Error", "Failed to read subsample divisor");
      return;
    }
    auto sampleRate = double(fBaseSamplingRate / fSubSampleDivisor);
    std::chrono::nanoseconds sample_interval(std::int64_t(1e9 / sampleRate));

    int nAvailableChannels = 0;
    status = amplifier_sdk->GetProperty(amplifier, PG_DEVICE, 0, DPROP_I32_AvailableChannels, &nAvailableChannels, sizeof(nAvailableChannels));
    if (status != AMP_OK) {
      on_error("BrainVision Amplifier Error", "Failed to read available channels");
      return;
    }

    int nIsEnabled = 0;
    //int nChannelType;
    float fResolution;
    int nDataType;
    std::vector<Channel> channels;
    //float temp = -1;
    //bool temp_bool;
    //int ref_set = false;
    for (auto i = 0u; i < uint32_t(nAvailableChannels); ++i) {
      status = amplifier_sdk->GetProperty(amplifier, PG_CHANNEL, i, CPROP_B32_RecordingEnabled, &nIsEnabled, sizeof(nIsEnabled));
      if (status != AMP_OK) {
        on_error("BrainVision Amplifier Error", "Failed to read channel enabled");
        return;
      }
      if (nIsEnabled) {
        status = amplifier_sdk->GetProperty(amplifier, PG_CHANNEL, i, CPROP_F32_Resolution, &fResolution, sizeof(fResolution));
        if (status != AMP_OK) {
          on_error("BrainVision Amplifier Error", "Failed to read channel resolution");
          return;
        }

        status = amplifier_sdk->GetProperty(amplifier, PG_CHANNEL, i, CPROP_I32_DataType, &nDataType, sizeof(nDataType));
        if (status != AMP_OK) {
          on_error("BrainVision Amplifier Error", "Failed to read channel data type");
          return;
        }

        channels.push_back(Channel{ double(fResolution), nDataType });
      }
    }
    int sampleBytes = 8;
    for (auto& channel : channels) {
      auto increment = datatype_to_bytes(channel.datatype);
      if (increment < 0) {
        on_error("BrainVision Amplifier Error", "Unexpected channel data type");
        return;
      }
      sampleBytes += increment;
    }

    std::vector<std::uint64_t> buffer(size_t(sampleBytes * sampleRate / 5) / 8 + 1, 0);
    auto buffer_bytes = reinterpret_cast<unsigned char*>(buffer.data());
    auto buffer_bytes_count = buffer.size() * sizeof(std::uint64_t);

    {
      std::lock_guard<std::mutex> lock(mutex);
      channel_names.assign(channels.size(), "");
      for (auto i = 0ull; i < channels.size(); ++i) {
        channel_names[i] = std::to_string(i);
      }
      sample_intervals.assign(channels.size(), sample_interval);
      spans.assign(channels.size(), std::span<const double>());
      output_data.clear();
      int_spans.assign(channels.size(), std::span<const int>());
      int_output_data.clear();
      scales.assign(channels.size(), 0.0);
    }

    status = amplifier_sdk->StartAcquisition(amplifier, RM_NORMAL);
    if (status != AMP_OK) {
      on_error("BrainVision Amplifier Error", "Failed to start acquisition");
      return;
    }

    Finally f2([&] {
      status = amplifier_sdk->StopAcquisition(amplifier);
      if (status != AMP_OK) {
        on_error("BrainVision Amplifier Error", "Failed to start acquisition");
        return;
      }
    });

    //auto last_log = std::chrono::steady_clock::now();
    //auto accum_samples = 0;
    //auto get_data_count = 0;
    //auto sample_mod = 0;
    busy = false;
    while(!stop_token.stop_requested()) {
      auto read_count = amplifier_sdk->GetData(amplifier, buffer_bytes, int32_t(buffer_bytes_count), int32_t(buffer_bytes_count/uint64_t(sampleBytes)));
      if(read_count < 0) {
        on_error("BrainVision Amplifier Error", "Failed to read data");
        return;
      }
      if (read_count == 0) {
        std::this_thread::sleep_for(10ms);
        continue;
      }
      //++get_data_count;
      auto now = std::chrono::steady_clock::now();
      //if (now - last_log >= 1s) {
      //  THALAMUS_LOG(info) << sample_mod << " " << accum_samples << " " << get_data_count;
      //  accum_samples = 0;
      //  sample_mod = 0;
      //  get_data_count = 0;
      //  last_log = now;
      //}

      auto num_samples = size_t(read_count) / size_t(sampleBytes);
      //sample_mod += read_count % sampleBytes;
      //accum_samples += num_samples;
      {
        std::unique_lock<std::mutex> lock(mutex);
        if(!is_int_data.has_value()) {
          is_int_data = true;
          for(auto& channel : channels) {
            switch (channel.datatype)
            {
            case DT_FLOAT32:
            case DT_FLOAT64:
              is_int_data = false;
              break;
            default:
              break;
            }
          }
        }

        _time = now.time_since_epoch();
        if(*is_int_data) {
          extract(int_output_data, int_spans, num_samples, channels, buffer_bytes, read_count);
        } else {
          extract(output_data, spans, num_samples, channels, buffer_bytes, read_count);
        }
        std::transform(channels.begin(), channels.end(), scales.begin(), [](auto& c) {
          return c.resolution;
        });
        busy = true;
        boost::asio::post(io_context, [this,node=outer->shared_from_this()] {
          std::lock_guard<std::mutex> lock2(mutex);
          outer->ready(outer);
          busy = false;
          cond.notify_all();
        });

        cond.wait(lock, [&] { return !busy; });
      }
    }
  }

  std::jthread acquisition_thread;

  void on_change(ObservableCollection *,
                 ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "BrainProductsNode::on_change");
    std::string key_str = std::get<std::string>(k);

    if (key_str == "Running") {
      is_running = std::get<bool>(v);
      if (is_running) {
        acquisition_thread = std::jthread([&] (std::stop_token stop_token) {
          acquisition_target(stop_token);
        });
      } else if(acquisition_thread.joinable()) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          busy = false;
          cond.notify_all();
        }
        acquisition_thread.request_stop();
        acquisition_thread.join();
      }
    }
  }
};

BrainProductsNode::BrainProductsNode(ObservableDictPtr state,
                                     boost::asio::io_context &io_context,
                                     NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

BrainProductsNode::~BrainProductsNode() {
  (*impl->state)["Running"].assign(false, [] {});
}

std::string BrainProductsNode::type_name() { return "BRAINPRODUCTS"; }

std::string_view BrainProductsNode::name(int channel) const {
  return impl->channel_names[size_t(channel)];
}
std::span<const std::string>
BrainProductsNode::get_recommended_channels() const {
  return std::span<const std::string>(impl->recommended_names.begin(),
                                      impl->recommended_names.end());
}

std::span<const double> BrainProductsNode::data(int index) const {
  return impl->spans[size_t(index)];
}

std::span<const int> BrainProductsNode::int_data(int index) const {
  return impl->int_spans[size_t(index)];
}

int BrainProductsNode::num_channels() const {
  return int(impl->channel_names.size());
}

void BrainProductsNode::inject(
    const thalamus::vector<std::span<double const>> &,
    const thalamus::vector<std::chrono::nanoseconds> &,
    const thalamus::vector<std::string_view> &) {
  THALAMUS_ABORT("BrainProductsNode::inject not implemented");
}

std::chrono::nanoseconds BrainProductsNode::sample_interval(int channel) const {
  return impl->sample_intervals[size_t(channel)];
}
std::chrono::nanoseconds BrainProductsNode::time() const {
  return impl->_time;
}

size_t BrainProductsNode::modalities() const {
  return infer_modalities<BrainProductsNode>();
}

bool BrainProductsNode::prepare() {
  return prepare_amplifier();
}

bool BrainProductsNode::is_int_data() const {
  return *impl->is_int_data;
}

bool BrainProductsNode::is_transformed() const {
  return true;
}
double BrainProductsNode::scale(int i) const {
  return impl->scales[size_t(i)];
}
double BrainProductsNode::offset(int) const {
  return 0.0;
}
