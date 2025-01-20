#pragma once
#include <perfetto.h>

#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable : 4005 )
#define TRACE_EVENT(category, name, ...) \
  PERFETTO_INTERNAL_SCOPED_TRACK_EVENT(category, name)
#pragma warning( pop )
#endif

PERFETTO_DEFINE_CATEGORIES(
  perfetto::Category("thalamus"));

namespace thalamus {
  unsigned long long get_unique_id();
}
