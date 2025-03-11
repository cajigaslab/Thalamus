#include <base_node.hpp>

namespace thalamus {

class TestPulseNode : public Node {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  TestPulseNode(ObservableDictPtr state, boost::asio::io_context &io_context,
                  NodeGraph *graph);
  ~TestPulseNode() override;
  static std::string type_name();
  size_t modalities() const override;
};

}
