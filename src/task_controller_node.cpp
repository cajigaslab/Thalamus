#include <task_controller_node.h>
#include <util.hpp>
#include <grpcpp/create_channel.h>
#include <tracing/tracing.h>
#include <task_controller.grpc.pb.h>
#include <modalities_util.h>

using namespace thalamus;

struct TaskControllerNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context& io_context;
  boost::signals2::scoped_connection state_connection;
  std::string address;
  std::shared_ptr<grpc::Channel> channel;
  NodeGraph* node_graph;
  std::atomic_bool running;
  Node* outer;
  std::unique_ptr<task_controller_grpc::TaskController::Stub> stub;

  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* node_graph, Node* outer)
    : state(state), io_context(io_context), node_graph(node_graph), running(false), outer(outer) {
    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Address") {
      address = std::get<std::string>(v);
      channel = node_graph->get_channel(address);
      stub = task_controller_grpc::TaskController::NewStub(channel);
    }
    else if (key_str == "Running") {
      if(!channel) {
        return;
      }

      running = std::get<bool>(v);
      grpc::ClientContext context;
      util_grpc::Empty response;
      if (running) {
        stub->start_execution(&context, util_grpc::Empty(), &response);
        return;
      } else {
        stub->stop_execution(&context, util_grpc::Empty(), &response);
        return;
      }
    }
  }
};

TaskControllerNode::TaskControllerNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* node_graph) : impl(new Impl(state, io_context, node_graph, this)) {
}

TaskControllerNode::~TaskControllerNode() {}

std::string TaskControllerNode::type_name() {
  return "TASK_CONTROLLER";
}

size_t TaskControllerNode::modalities() const { return infer_modalities<TaskControllerNode>(); }
