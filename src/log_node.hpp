#pragma once

#include <base_node.hpp>
#include <text_node.hpp>

namespace thalamus {
  class LogNode : public Node, public TextNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    LogNode(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph);
    ~LogNode();
    std::string_view text() const override;
    bool has_text_data() const override;
    boost::json::value process(const boost::json::value&) override;
    std::chrono::nanoseconds time() const override;
    static std::string type_name();
    size_t modalities() const override;
  };
}
