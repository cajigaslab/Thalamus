#include <base_node.hpp>
#include <modalities_util.hpp>
#include <thalamus/tracing.hpp>
#include <util.hpp>

namespace thalamus {

Node::~Node() {}
NodeGraph::~NodeGraph() {}
NoneNode::NoneNode(ObservableDictPtr, boost::asio::io_context &, NodeGraph *) {}
size_t NoneNode::modalities() const { return 0; }

std::vector<std::weak_ptr<ObservableDict>>
get_nodes(ObservableList *nodes, const std::vector<std::string> &names) {
  std::vector<std::weak_ptr<ObservableDict>> targets;
  for (auto raw_token : names) {
    auto token = absl::StripAsciiWhitespace(raw_token);
    auto i = std::find_if(nodes->begin(), nodes->end(), [&](auto node) {
      ObservableDictPtr dict = node;
      std::string name = dict->at("name");
      auto stripped_name = absl::StripAsciiWhitespace(name);
      return stripped_name == token;
    });

    if (i != nodes->end()) {
      ObservableDictPtr temp = *i;
      targets.push_back(std::weak_ptr<ObservableDict>(temp));
    }
  }

  return targets;
}
} // namespace thalamus
