#pragma once
#include <perfetto.h>

//TRACE_EVENT is causing compiler errors when used inside a lambda on behave2.
//Make TRACE_EVENT default to a no op until this is solved.
#ifdef _WIN32
#define THALAMUS_NO_TRACING
#endif

#ifdef THALAMUS_NO_TRACING
#pragma warning( push )
#pragma warning( disable : 4005)
#define TRACE_EVENT(...)
#define TRACE_EVENT_BEGIN(...)
#define TRACE_EVENT_END(...)
#pragma warning( pop )
#endif

PERFETTO_DEFINE_CATEGORIES(
  perfetto::Category("thalamus"));

namespace thalamus {
  unsigned long long get_unique_id();
}