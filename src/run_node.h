#include <base_node.h>

namespace thalamus {

  class RunNode : public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    RunNode(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph);
    ~RunNode();
    static std::string type_name();
    size_t modalities() const override { return 0; }
  };
}
