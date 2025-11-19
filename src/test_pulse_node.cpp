#include <thalamus/tracing.hpp>
#include <test_pulse_node.hpp>
#include <analog_node.hpp>
#include <stim_node.hpp>
#include <thalamus.pb.h>
#include <thalamus/nidaqmx.hpp>

using namespace thalamus;

struct TestPulseNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context & io_context;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection get_output_connection;
  NodeGraph *graph;
  StimNode* stim_node;
  std::weak_ptr<Node> weak_output;
  double last_value = 0;
  std::chrono::nanoseconds last_time = 0ns;
  TaskHandle	taskHandle;

  static int32 CVICALLBACK ChangeDetectionCallback(TaskHandle, int32, void *callbackData) {
    return reinterpret_cast<TestPulseNode::Impl*>(callbackData)->on_edge();
  }

  int32 on_edge() {
    auto now = std::chrono::steady_clock::now();
    boost::asio::post(io_context, [this,now] {
      graph->log("PULSE");
      thalamus_grpc::StimRequest request;
      request.set_trigger(0);
      stim_node->stim(std::move(request));
      last_time = now.time_since_epoch();
    });
    return 0;
  }

  Impl(ObservableDictPtr _state, boost::asio::io_context & _io_context,
       NodeGraph *_graph, TestPulseNode *) : state(_state), io_context(_io_context), graph(_graph) {
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));

    auto api = DAQmxAPI::get_singleton();

    auto err = api->DAQmxCreateTask("",&taskHandle);
    THALAMUS_ASSERT(err >= 0, "");

    err = api->DAQmxCreateDIChan(taskHandle, "Dev1/port0/line0", "", DAQmx_Val_ChanPerLine);
    THALAMUS_ASSERT(err >= 0, "");

    err = api->DAQmxCfgChangeDetectionTiming(taskHandle,"Dev1/port0/line0","",DAQmx_Val_ContSamps,1);
    THALAMUS_ASSERT(err >= 0, "");

    err = api->DAQmxRegisterSignalEvent(taskHandle,DAQmx_Val_ChangeDetectionEvent,0,ChangeDetectionCallback,this);
    THALAMUS_ASSERT(err >= 0, "");

    err = api->DAQmxStartTask(taskHandle);
    THALAMUS_ASSERT(err >= 0, "");
  }

  ~Impl() {
    auto api = DAQmxAPI::get_singleton();
    THALAMUS_ASSERT(api->DAQmxStopTask(taskHandle) >= 0, "");
    THALAMUS_ASSERT(api->DAQmxClearTask(taskHandle) >= 0, "");
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "NidaqNode::on_change");
    auto key_str = std::get<std::string>(k);
    if (key_str == "Output") {
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
        span->set_name("Dev1/ao0");
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
