#pragma once

//perfetto doesn't work in msvc
#if !defined(__clang__) and defined(_MSC_VER)
#define TRACE_EVENT(...)
#define TRACE_EVENT_BEGIN(...)
#define TRACE_EVENT_END(...)
namespace perfetto {
  struct Track {
    Track(int i) {}
  };
}
#else
#include <perfetto.h>
PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("thalamus")
        .SetDescription("General Thalamus events"),
    perfetto::Category("intan")
        .SetDescription("Intan node events"),
    perfetto::Category("storage")
        .SetDescription("Storage node events"),
    perfetto::Category("nidaq")
        .SetDescription("NIDAQ node events"));
#endif

namespace thalamus {
  unsigned long long get_unique_id();
}
