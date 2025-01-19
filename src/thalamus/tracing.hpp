#pragma once
#include <perfetto.h>

#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable : 4005 )
#define TRACE_EVENT(...)
#pragma warning( pop )
#endif

PERFETTO_DEFINE_CATEGORIES(
  perfetto::Category("thalamus"));

namespace thalamus {
  unsigned long long get_unique_id();
}
