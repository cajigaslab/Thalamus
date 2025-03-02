#include <test_pulse_node.hpp>
#include <thalamus/tracing.hpp>
#include <analog_node.hpp>
#include <stim_node.hpp>
#include <thalamus.pb.h>

using namespace thalamus;

struct TestPulseNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection source_connection;
  boost::signals2::scoped_connection get_input_connection;
  boost::signals2::scoped_connection get_output_connection;
  NodeGraph *graph;
  StimNode* stim_node;
  std::weak_ptr<Node> weak_output;
  double last_value = 0;
  std::chrono::nanoseconds last_time = 0ns;

  Impl(ObservableDictPtr _state, boost::asio::io_context &,
       NodeGraph *_graph, TestPulseNode *) : state(_state), graph(_graph) {
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  void on_data(Node*, AnalogNode* source) {
    auto lock = weak_output.lock();
    if(!lock) {
      return;
    }
    if(source->num_channels() < 1) {
      return;
    }
    auto data = source->data(0);
    if(data.empty()) {
      return;
    }
    auto current_value = data.back();
    auto now = source->time();
    if(current_value - last_value > 2 && now - last_time > 500ms) {
      thalamus_grpc::StimRequest request;
      request.set_trigger(0);
      stim_node->stim(std::move(request));
      last_time = now;
    }
    last_value = current_value;
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "NidaqNode::on_change");
    auto key_str = std::get<std::string>(k);
    if (key_str == "Input") {
      std::string source_str = std::get<std::string>(v);
      source_connection.disconnect();

      get_input_connection = graph->get_node_scoped(source_str, [&](auto node) {
        auto locked_source = node.lock();
        auto analog_node = std::dynamic_pointer_cast<AnalogNode>(locked_source);
        if (!locked_source || analog_node == nullptr) {
          return;
        }
        source_connection = locked_source->ready.connect(
            std::bind(&Impl::on_data, this, _1, analog_node.get()));
      });
    } else if (key_str == "Output") {
      std::string output_str = std::get<std::string>(v);

      get_output_connection = graph->get_node_scoped(output_str, [&](auto node) {
        auto locked_source = node.lock();
        auto maybe_stim_node = std::dynamic_pointer_cast<StimNode>(locked_source);
        if (!locked_source || maybe_stim_node == nullptr) {
          return;
        }
        thalamus_grpc::StimRequest request;
        auto declaration = request.mutable_declaration();
        declaration->set_id(0);
        auto data = declaration->mutable_data();
        data->add_data(5);
        data->add_data(0);
        auto span = data->add_spans();
        span->set_begin(0);
        span->set_end(2);
        span->set_name("Dev2/ao0");
        data->add_sample_intervals(300000000);
        maybe_stim_node->stim(std::move(request));

        thalamus_grpc::StimRequest arm_request;
        arm_request.set_trigger(0);
        maybe_stim_node->stim(std::move(arm_request));

        weak_output = node;
        stim_node = maybe_stim_node.get();
      });
    }
  }
};

TestPulseNode::TestPulseNode(ObservableDictPtr state,
                     boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

TestPulseNode::~TestPulseNode() {}

std::string TestPulseNode::type_name() {
  return "TEST_PULSE_NODE";
}

size_t TestPulseNode::modalities() const {
  return 0;
}
