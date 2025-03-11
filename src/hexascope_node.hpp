#pragma once

#include <base_node.hpp>
#include <state.hpp>
#include <string>
#include <thalamus.pb.h>
#include <thalamus_asio.hpp>

namespace thalamus {
class HexascopeNode : public Node {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  HexascopeNode(ObservableDictPtr state, boost::asio::io_context &io_context,
                NodeGraph *);
  ~HexascopeNode() override;
  static std::string type_name();
  static bool prepare();
  boost::json::value process(const boost::json::value &) override;
  size_t modalities() const override;
};
} // namespace thalamus
