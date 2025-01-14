#include <thalamus/tracing.hpp>
#include <atomic>

namespace thalamus {
  static std::atomic_ullong next_id = 0;

  unsigned long long get_unique_id() {
    return ++next_id;
  }
}