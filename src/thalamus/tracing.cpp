#include <atomic>
#include <thalamus/tracing.hpp>

namespace thalamus {
static std::atomic_ullong next_id = 0;

uint64_t get_unique_id() { return ++next_id; }
} // namespace thalamus
