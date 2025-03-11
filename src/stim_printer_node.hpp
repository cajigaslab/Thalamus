#pragma once
#include <stim_node.hpp>

namespace thalamus {

class StimPrinterNode : public Node, public StimNode {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  StimPrinterNode(ObservableDictPtr, boost::asio::io_context &, NodeGraph *);
  ~StimPrinterNode() override;

  std::future<thalamus_grpc::StimResponse>
  stim(thalamus_grpc::StimRequest &&) override;

  static std::string type_name();
  size_t modalities() const override;
};
} // namespace thalamus
