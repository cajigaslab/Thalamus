#include <base_node.hpp>

namespace thalamus {

class Run2Node : public Node {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  Run2Node(ObservableDictPtr state, boost::asio::io_context &, NodeGraph *graph);
  ~Run2Node() override;
  static std::string type_name();
  size_t modalities() const override { return 0; }
};
} // namespace thalamus
