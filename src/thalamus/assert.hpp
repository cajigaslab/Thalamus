#pragma once
#include <limits>
#include <absl/strings/str_format.h>
#include <boost/stacktrace.hpp>

#ifdef CODE_COVERAGE
template<typename... Args>
void THALAMUS_ASSERT(bool condition, const absl::FormatSpec<Args...>& format, const Args&... args) {
  if (!(condition)) {
    THALAMUS_LOG(fatal) << absl::StrFormat(format, args...) << "\n"
                        << boost::stacktrace::stacktrace(
                               2, std::numeric_limits<size_t>::max());
    std::abort();
  }
}
#else
#define THALAMUS_ASSERT(condition, ...)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
      THALAMUS_LOG(fatal) << absl::StrFormat("" __VA_ARGS__) << "\n"           \
                          << boost::stacktrace::stacktrace(                    \
                                 2, std::numeric_limits<size_t>::max());       \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
#endif
