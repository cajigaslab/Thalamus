#include <delsys_node.hpp>
#include <modalities_util.hpp>

namespace thalamus {
struct DelsysNode::Impl {
  std::string location;
  boost::asio::io_context& io_context;
  ObservableDictPtr state;
  DelsysNode * outer;
  NodeGraph* graph;
  boost::signals2::scoped_connection state_connection;
  
  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       DelsysNode *_outer, NodeGraph *_graph)
      : io_context(_io_context), state(_state), outer(_outer), graph(_graph) {
    using namespace std::placeholders;
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Location") {
      location = std::get<std::string>(v);
      outer->channels_changed(outer);
      outer->ready(outer);
    }
  }
};

DelsysNode::DelsysNode(ObservableDictPtr state, boost::asio::io_context & io_context, NodeGraph * graph)
    : impl(new Impl(state, io_context, this, graph)) {

}

DelsysNode::~DelsysNode() {}

void DelsysNode::inject(const thalamus::vector<std::span<double const>> &,
            const thalamus::vector<std::chrono::nanoseconds> &,
            const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

std::string_view DelsysNode::name(int) const {
  THALAMUS_ASSERT(false, "Unimplemented");
}

int DelsysNode::num_channels() const {
  THALAMUS_ASSERT(false, "Unimplemented");
}
std::chrono::nanoseconds DelsysNode::sample_interval(int) const {
  THALAMUS_ASSERT(false, "Unimplemented");
}
std::chrono::nanoseconds DelsysNode::time() const {
  THALAMUS_ASSERT(false, "Unimplemented");
}
std::span<const double> DelsysNode::data(int) const {
  THALAMUS_ASSERT(false, "Unimplemented");
}

std::string DelsysNode::type_name() { return "DELSYS"; }
size_t DelsysNode::modalities() const { return infer_modalities<DelsysNode>(); }

std::string_view DelsysNode::redirect() const {
  return impl->location;
}

std::string_view DelsysNode::text() const {
  return "";
}

bool DelsysNode::has_text_data() const {
  return false;
}

} // namespace thalamus
