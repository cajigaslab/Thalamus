#include <ophanim_node.h>
#include <util.h>
#include <grpcpp/create_channel.h>
#include <ophanim.grpc.pb.h>
#include <tracing/tracing.h>
#include <modalities_util.h>

using namespace thalamus;

struct OphanimNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context& io_context;
  boost::signals2::scoped_connection state_connection;
  std::string address;
  std::shared_ptr<grpc::Channel> channel;
  std::unique_ptr<ophanim_grpc::Ophanim::Stub> stub;
  NodeGraph* node_graph;
  std::atomic_bool running;
  Node* outer;
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
      stub = ophanim_grpc::Ophanim::NewStub(channel);
      //channel = node_graph->get_channel(value);
    }
    else if (key_str == "Running") {
      if(!channel) {
        return;
      }

      running = std::get<bool>(v);
      grpc::ClientContext context;
      util_grpc::Empty response;
      if (running) {
        stub->play_all(&context, util_grpc::Empty(), &response);
        return;
      } else {
        stub->stop_all(&context, util_grpc::Empty(), &response);
        return;
      }
    }
  }
};

OphanimNode::OphanimNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* node_graph) : impl(new Impl(state, io_context, node_graph, this)) {
}

OphanimNode::~OphanimNode() {}

std::string OphanimNode::type_name() {
  return "OPHANIM";
}

size_t OphanimNode::modalities() const { return infer_modalities<OphanimNode>(); }
