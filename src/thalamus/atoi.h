#include <absl/strings/numbers.h>

namespace thalamus {
template <typename T> T parse_number(const std::string_view &text) {
  T result;
  if constexpr (std::is_integral<T>::value) {
    auto success = absl::SimpleAtoi(text, &result);
    THALAMUS_ASSERT(success, "atoi failed");
  } else if constexpr (std::is_same<T, float>::value) {
    auto success = absl::SimpleAtof(text, &result);
    THALAMUS_ASSERT(success, "atof failed");
  } else if constexpr (std::is_same<T, double>::value) {
    auto success = absl::SimpleAtod(text, &result);
    THALAMUS_ASSERT(success, "atod failed");
  }
  return result;
}
} // namespace thalamus
