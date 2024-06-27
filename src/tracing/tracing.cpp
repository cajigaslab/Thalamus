#include <tracing/tracing.h>
#include <absl/strings/str_replace.h>
#include <absl/types/variant.h>
#include <assert.h>
#if defined(_WIN32) || defined(__APPLE__)
#include <filesystem>
namespace filesystem = std::filesystem;
#else
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#endif
#include <condition_variable>
#include <tracing/iclock.h>
#include <mutex>
#include <rtc_base/thread.h>
#include <rtc_base/trace_event.h>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/prctl.h>
#endif

namespace tracing
{
  static Counter COUNTERS[1024];
  std::atomic_ullong NEXT_COUNTER = 0;

  Counter* allocate_counter(const char* name)
  {
    auto result = &COUNTERS[NEXT_COUNTER++];
    result->name = name;
    result->count = 0;
    return result;
  }

  void counter_thread() {

  }

  static const unsigned char ONE[] = { 49, 0 };

  static const unsigned char* CategoryEnabled(const char* name)
  {
    return ONE;
  }

  typedef absl::variant<char const*, std::string, double, bool, unsigned long long, long long> ValueVaraint;

  struct TraceEvent
  {
    char phase;
    const unsigned char* category;
    const char* name;
    int num_args;
    std::pair<const char*, ValueVaraint> args[2];
    int64_t tsUs;
    std::thread::id tid;
    unsigned long long id;
  };

  static std::string escape(const std::string& input)
  {
    return absl::StrReplaceAll(input, { { "\"", "\\\"" }, { "\\", "\\\\" } });
  }

  struct JsonVisitor
  {
    std::ostream& output;

    template<typename T>
    void operator()(T const& arg)
    {
      output << arg;
    }
  };

  template<>
  void JsonVisitor::operator() < char const* > (char const* const& arg)
  {
    output << "\"" << escape(arg) << "\"";
  }

  template<>
  void JsonVisitor::operator() < std::string > (std::string const& arg)
  {
    output << "\"" << escape(arg) << "\"";
  }

  static std::mutex mutex;
  static std::mutex thread_name_mutex;
  static bool enabled = false;
  static std::vector<TraceEvent> traceEvents;
  static std::unordered_map<std::thread::id, std::string> threadIds;
  static int64_t startUs;
  static int64_t durationUs;
  static std::string outputFolderName;
  static std::thread serializationThread;
  static bool collecting;
  static bool serialized;
  static bool running;
  static std::condition_variable serializedConditionVariable;
  static IClock* clock;
  static int serializeCounter = 0;

  static void serializeEvent(std::ostream& output, const TraceEvent& e) {
    output << "{";
    output << "\"name\":\"" << escape(e.name) << "\",";
    output << "\"cat\":\"" << e.category << "\",";
    output << "\"ph\":\"" << e.phase << "\",";
    output << "\"ts\":" << e.tsUs << ",";
    if (e.id) {
      output << "\"id\":" << e.id << ",";
    }
    output << "\"pid\":1,";

    std::stringstream tidStream;
    tidStream << e.tid;

    output << "\"tid\":" << std::strtoul(tidStream.str().c_str(), nullptr, 16) << ",";
    output << "\"args\":{";

    auto argFirst = true;
    std::for_each(e.args, e.args + e.num_args, [&](auto& arg) {
      if (!argFirst)
      {
        output << ",";
      }
      argFirst = false;
      output << "\"" << escape(arg.first) << "\":";
      absl::visit(JsonVisitor{ output }, arg.second);
    });

    output << "}}\n";
  }

  static bool is_running() {
    std::lock_guard<std::mutex> lock(mutex);
    return running;
  }

  static void serializeEvents()
  {
    auto thread_id = std::this_thread::get_id();
    filesystem::create_directories(outputFolderName);
    std::vector<TraceEvent> localTraceEvents;
    localTraceEvents.reserve(1048576);

    while (is_running()) {
      std::stringstream stream;
      stream << outputFolderName << "/" << serializeCounter++ << ".json";
      std::ofstream output(stream.str());

      output << "{\"traceEvents\":[";
      auto first = true;
      int64_t lastUs = 0;
      int64_t fileStartUs = -1;

      while (is_running() && lastUs <= durationUs) {
        auto interval = std::chrono::milliseconds(10);
        auto start = std::chrono::high_resolution_clock::now();
        auto end = start;
        std::chrono::milliseconds duration;
        {
          std::lock_guard<std::mutex> lock(mutex);
          localTraceEvents.swap(traceEvents);
        }
        for (const auto& e : localTraceEvents)
        {
          if (!first)
          {
            output << ",";
          }
          first = false;
          serializeEvent(output, e);
          if (fileStartUs < 0) {
            fileStartUs = e.tsUs;
          }
          lastUs = e.tsUs - fileStartUs;
        }
        localTraceEvents.clear();

        auto counter_ts = clock->GetEpochUs() - startUs;
        auto num_counters = NEXT_COUNTER.load();
        for (size_t i = 0; i < num_counters; ++i) {
          auto count = COUNTERS[i].count.exchange(0);
          if (!count) {
            continue;
          }
          auto& name = COUNTERS[i].name;
          if (!first)
          {
            output << ",";
          }
          first = false;
          serializeEvent(output, TraceEvent{ 'C', ONE, name, 1, {{name, count}}, counter_ts, thread_id});
        }

        end = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (duration < interval) {
          std::this_thread::sleep_for(interval - duration);
        }
      }
       
      {
        std::lock_guard<std::mutex> lock(thread_name_mutex);
        for (auto& pair : threadIds)
        {
          if (!first)
          {
            output << ",";
          }
          first = false;
          serializeEvent(output, TraceEvent{ 'M', ONE, "thread_name", 1, { { "name", pair.second } }, 0, pair.first });
        }
      }

      output << "]}";
    }
  }

  static void AddTraceEvent(char phase, const unsigned char* category_enabled, const char* name, unsigned long long id, int num_args, const char** arg_names, const unsigned char* arg_types,
    const unsigned long long* arg_values, unsigned char flags)
  {
    assert(num_args < 3);

    std::lock_guard<std::mutex> lock(mutex);

    auto now = clock->GetEpochUs();
    auto sinceStart = now - startUs;

    auto threadId = std::this_thread::get_id();

    traceEvents.push_back(TraceEvent{ phase, category_enabled, name, num_args, {}, sinceStart, threadId, id });

    for (auto i = 0; i < num_args; ++i)
    {
      webrtc::trace_event_internal::TraceValueUnion traceUnion;
      traceUnion.as_uint = arg_values[i];
      switch (arg_types[i])
      {
      case TRACE_VALUE_TYPE_STRING:
        traceEvents.back().args[i] = { arg_names[i], traceUnion.as_string };
        break;
      case TRACE_VALUE_TYPE_COPY_STRING:
        traceEvents.back().args[i] = { arg_names[i], std::string(traceUnion.as_string) };
        break;
      case TRACE_VALUE_TYPE_DOUBLE:
        traceEvents.back().args[i] = { arg_names[i], traceUnion.as_double };
        break;
      case TRACE_VALUE_TYPE_BOOL:
        traceEvents.back().args[i] = { arg_names[i], traceUnion.as_bool };
        break;
      case TRACE_VALUE_TYPE_UINT:
        traceEvents.back().args[i] = { arg_names[i], traceUnion.as_uint };
        break;
      case TRACE_VALUE_TYPE_INT:
        traceEvents.back().args[i] = { arg_names[i], traceUnion.as_int };
        break;
      }
    }
  }

  void SetCurrentThreadName(const std::string& name)
  {
    std::lock_guard<std::mutex> lock(thread_name_mutex);
    threadIds[std::this_thread::get_id()] = name;
#ifdef _WIN32
    std::wstring wname(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wname.c_str());
#else
    prctl(PR_SET_NAME,thread_name.c_str(),0,0,0);
#endif
  }

  void Enable(int seconds, const std::string& folderName)
  {
    traceEvents.clear();
    traceEvents.reserve(1048576);
    durationUs = 1'000'000 * seconds;
    outputFolderName = folderName;

    webrtc::SetupEventTracer(CategoryEnabled, AddTraceEvent);
    enabled = true;
  }

  void Disable()
  {
    webrtc::SetupEventTracer(nullptr, nullptr);
    enabled = false;
  }

  bool IsEnabled()
  {
    return enabled;
  }

  void Start()
  {
    std::lock_guard<std::mutex> lock(mutex);

    assert(clock);

    startUs = clock->GetEpochUs();
    running = true;
    serializeCounter = 0;
    serializationThread = std::thread(serializeEvents);
  }

  void Stop()
  {
    std::lock_guard<std::mutex> lock(mutex);

    running = false;
  }

  void Wait()
  {
    if (serializationThread.joinable())
    {
      serializationThread.join();
    }
  }

  void SetClock(IClock* theClock)
  {
    clock = theClock;
  }
}
