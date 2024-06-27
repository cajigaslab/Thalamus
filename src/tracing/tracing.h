#pragma once
#include <string>
#include <atomic>

#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#else
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#endif

#include <rtc_base/trace_event.h>

#ifdef __clang__
#pragma GCC diagnostic pop
#else
#pragma pop_macro("min")
#pragma pop_macro("max")
#endif

class IClock;

#define TRACE_COUNTER_SLOW(name) {\
  static auto* counter = tracing::allocate_counter(name); \
  ++counter->count; \
}

namespace tracing
{
  void Enable(int seconds, const std::string& folderName);
  void Disable();
  bool IsEnabled();

  void Start();
  void Stop();
  void Wait();
  void SetClock(IClock*);

  void SetCurrentThreadName(const std::string& name);

  struct Counter
  {
    const char* name;
    std::atomic_ullong count;
  };
  Counter* allocate_counter(const char* name);
}
