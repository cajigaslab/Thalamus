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
};
} // namespace thalamus
