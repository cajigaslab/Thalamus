#pragma once

#include <string>
#include <thalamus.pb.h>
#include <thalamus_asio.h>
#include <base_node.h>
#include <state.hpp>

namespace thalamus {
  class HexascopeNode : public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    HexascopeNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*);
    ~HexascopeNode();
    static std::string type_name();
    static bool prepare();
    boost::json::value process(const boost::json::value&) override;
    size_t modalities() const override;
  };
}

