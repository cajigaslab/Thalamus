#pragma once

#include <span>
#include <chrono>
#include <string_view>

namespace thalamus {

class BlobNode {
public:
  virtual ~BlobNode();
  virtual std::span<const uint8_t> body() const = 0;
  virtual std::string_view mime() const = 0;
  virtual uint32_t stream() const = 0;
  virtual bool has_blob_data() const = 0;
  virtual std::chrono::nanoseconds time() const = 0;
};

} // namespace thalamus
