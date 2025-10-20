#pragma once
#include <thalamus/log.hpp>
#include <limits>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <absl/strings/str_format.h>
#include <boost/exception/get_error_info.hpp>
#include <boost/exception/info.hpp>
#include <boost/stacktrace.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

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

#define THALAMUS_ABORT(...)                                                    \
  THALAMUS_LOG(fatal) << absl::StrFormat("" __VA_ARGS__) << "\n"               \
                      << boost::stacktrace::stacktrace(                        \
                             2, std::numeric_limits<size_t>::max());           \
  std::abort()

#define THALAMUS_ABORT_WITH_SKIP(skip, ...)                                    \
  THALAMUS_LOG(fatal) << absl::StrFormat("" __VA_ARGS__) << "\n"               \
                      << boost::stacktrace::stacktrace(                        \
                             2 + skip, std::numeric_limits<size_t>::max());    \
  std::abort()
