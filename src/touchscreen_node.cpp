#include <modalities_util.hpp>
#include <touchscreen_node.hpp>

namespace thalamus {
struct TouchScreenNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  TouchScreenNode *outer;
  ObservableListPtr transform;
  NodeGraph *graph;
  AnalogNode *source;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context &, NodeGraph *_graph,
       TouchScreenNode *_outer)
      : state(_state), outer(_outer), graph(_graph) {

    mat[0] = {1.0, 0.0, 0.0};
    mat[1] = {0.0, 1.0, 0.0};
    mat[2] = {0.0, 0.0, 1.0};

    state_connection = state->recursive_changed.connect(
        std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }
  AnalogNode *current_node;

  boost::signals2::scoped_connection get_source_connection;
  std::array<std::array<double, 3>, 3> mat;
  std::pair<double, double> input;
  std::pair<double, double> output;

  void on_data(Node *) {
    if (!source->has_analog_data()) {
      return;
    }
    if (source->num_channels() < 2) {
      return;
    }
    auto x_channel = source->data(0);
    auto y_channel = source->data(1);
    if (!x_channel.empty()) {
      input.first = x_channel.front();
    }
    if (!y_channel.empty()) {
      input.second = y_channel.front();
    }
    if (input.first < -4 || input.second < -4) {
      output.first = input.first;
      output.second = input.second;
      outer->ready(outer);
      return;
    }
    output.first =
        mat[0][0] * input.first + mat[0][1] * input.second + mat[0][2];
    output.second =
        mat[1][0] * input.first + mat[1][1] * input.second + mat[1][2];
    outer->ready(outer);
  }

  void on_change(ObservableCollection *_source, ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    if (_source == transform.get()) {
      auto row = std::get<ObservableListPtr>(v);
      row->recap(std::bind(&Impl::on_change, this, row.get(), _1, _2, _3));
      return;
    } else if (_source->parent == transform.get()) {
      auto row_v = transform->key_of(*_source);
      THALAMUS_ASSERT(row_v, "Row not found in transform");
      auto row = size_t(std::get<long long>(*row_v));
      auto column = size_t(std::get<long long>(k));
      auto value = std::get<double>(v);
      mat[row][column] = value;
      return;
    }

    auto key_str = std::get<std::string>(k);
    if (key_str == "Source") {
      auto val_str = std::get<std::string>(v);
      get_source_connection =
          graph->get_node_scoped(val_str, [&](std::weak_ptr<Node> weak) {
            auto lock = weak.lock();
            auto analog = node_cast<AnalogNode *>(lock.get());
            if (!analog) {
              return;
            }
            this->source = analog;
            lock->ready.connect(std::bind(&Impl::on_data, this, _1));
          });
    } else if (key_str == "Transform") {
      transform = std::get<ObservableListPtr>(v);
      transform->recap(
          std::bind(&Impl::on_change, this, transform.get(), _1, _2, _3));
    }
  }
};

TouchScreenNode::TouchScreenNode(ObservableDictPtr state,
                                 boost::asio::io_context &io_context,
                                 NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

TouchScreenNode::~TouchScreenNode() {}

std::string TouchScreenNode::type_name() { return "TOUCH_SCREEN"; }

std::chrono::nanoseconds TouchScreenNode::time() const {
  return impl->source->time();
}

std::span<const double> TouchScreenNode::data(int channel) const {
  if (channel == 0) {
    return std::span<const double>(&impl->output.first,
                                   &impl->output.first + 1);
  } else if (channel == 1) {
    return std::span<const double>(&impl->output.second,
                                   &impl->output.second + 1);
  } else {
    return std::span<const double>();
  }
}

int TouchScreenNode::num_channels() const { return 2; }

std::string_view TouchScreenNode::name(int channel) const {
  if (channel == 0) {
    return "X";
  } else if (channel == 1) {
    return "Y";
  } else {
    return "";
  }
}

std::chrono::nanoseconds TouchScreenNode::sample_interval(int) const {
  return 0ns;
}

void TouchScreenNode::inject(const thalamus::vector<std::span<double const>> &,
                             const thalamus::vector<std::chrono::nanoseconds> &,
                             const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

bool TouchScreenNode::has_analog_data() const { return true; }

size_t TouchScreenNode::modalities() const {
  return infer_modalities<TouchScreenNode>();
}
} // namespace thalamus
