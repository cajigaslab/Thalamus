#include <base_node.hpp>

namespace thalamus {
  class TaskControllerNode : public Node {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    TaskControllerNode(ObservableDictPtr, boost::asio::io_context&, NodeGraph*);
    ~TaskControllerNode();
    static std::string type_name();
    size_t modalities() const override;
  };
}
