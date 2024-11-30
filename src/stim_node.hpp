#pragma once
#include <base_node.hpp>

namespace thalamus {
  class StimNode {
  public:
    virtual ~StimNode() {}
    virtual thalamus_grpc::StimResponse stim(const thalamus_grpc::StimRequest&) = 0;
  };
}
