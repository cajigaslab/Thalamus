#pragma once
#include <thalamus/base_node.hpp>

namespace thalamus {
class StimNode {
public:
  virtual ~StimNode();
  virtual std::future<thalamus_grpc::StimResponse>
  stim(thalamus_grpc::StimRequest &&) = 0;
};
} // namespace thalamus
