#pragma once

#include <chrono>
#include <string_view>

namespace thalamus {
class TextNode {
public:
  virtual ~TextNode();
  virtual std::string_view text() const = 0;
  virtual bool has_text_data() const = 0;
  virtual std::chrono::nanoseconds time() const = 0;
  // The text's time on the Thalamus that first produced it, if it came from
  // another one, else 0.
  virtual std::chrono::nanoseconds remote_time() const { return std::chrono::nanoseconds(0); }
};
} // namespace thalamus
