#include <base_node.h>
#include <text_node.h>

namespace thalamus {
  class LogNode : public Node, public TextNode {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    LogNode(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph);
    virtual ~LogNode() {};
    std::string_view text() const override;
    bool has_text_data() const override;
    boost::json::value process(const boost::json::value&) override;
    std::chrono::nanoseconds time() const;
  };
}
