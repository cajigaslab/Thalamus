#pragma once
#include <perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
  perfetto::Category("thalamus"));

namespace thalamus {
  unsigned long long get_unique_id();
}
