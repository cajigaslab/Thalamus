#include <thalamus/nidaqmx.hpp>
#include <mutex>
#include <string>
#include <util.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

using namespace thalamus;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef _WIN32
    template <typename T> T load_function(::HMODULE library_handle, const std::string &func_name) {
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
    template <typename T> T load_function(void* library_handle, const std::string &func_name) {
      auto result =
          reinterpret_cast<T>(dlsym(library_handle, func_name.c_str()));
      if (!result) {
        THALAMUS_LOG(info) << "Failed to load " << func_name << ".  "
                           << "NIDAQ disabled";
      }
      return result;
    }
#endif

DAQmxAPI* DAQmxAPI::get_singleton() {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  static bool has_run = false;
  static DAQmxAPI* singleton = nullptr;
  if (has_run) {
    return singleton;
  }
  DAQmxAPI local;
  has_run = true;
#ifdef _WIN32
      auto library_handle = LoadLibrary("nicaiu");
#else
      std::string nidaqmx_path = "/usr/lib/x86_64-linux-gnu/libnidaqmx.so.25.5.0";
      auto library_handle = dlopen(nidaqmx_path.c_str(), RTLD_NOW);
#endif
  if (!library_handle) {
    THALAMUS_LOG(info)
        << "Couldn't find nicaiu.dll.  National Instruments features disabled";
    return nullptr;
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
    local.name = load_function<decltype(local.name)>(library_handle, #name);                     \
    if (!local.name) {                                                  \
      return nullptr;                                                                  \
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
  LOAD_FUNC(DAQmxWriteDigitalU32);
  LOAD_FUNC(DAQmxGetTaskNumDevices);
  LOAD_FUNC(DAQmxGetNthTaskDevice);
  LOAD_FUNC(DAQmxGetDevProductCategory);
  LOAD_FUNC(DAQmxSetRefClkSrc);
  LOAD_FUNC(DAQmxSetRefClkRate);
  LOAD_FUNC(DAQmxSetWriteRegenMode);

  singleton = new DAQmxAPI(local);

  char buffer[1024];
  auto z = singleton->DAQmxGetSysDevNames(buffer, sizeof(buffer));
  THALAMUS_LOG(info) << z << " " << buffer;

  THALAMUS_LOG(info) << "DAQmx API loaded";
  return singleton;
}
