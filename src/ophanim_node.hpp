#include <base_node.hpp>

namespace thalamus {
class OphanimNode : public Node {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  OphanimNode(ObservableDictPtr, boost::asio::io_context &, NodeGraph *);
  ~OphanimNode() override;
  static std::string type_name();
  size_t modalities() const override;
};
} // namespace thalamus
