#include <modalities_util.hpp>
#include <ophanim_node.hpp>
#include <util.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <grpcpp/create_channel.h>
#include <ophanim.grpc.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace thalamus;

struct OphanimNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context &io_context;
  boost::signals2::scoped_connection state_connection;
  std::string address;
  std::shared_ptr<grpc::Channel> channel;
  std::unique_ptr<ophanim_grpc::Ophanim::Stub> stub;
  NodeGraph *node_graph;
  std::atomic_bool running;
  Node *outer;
  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_node_graph, Node *_outer)
      : state(_state), io_context(_io_context), node_graph(_node_graph),
        running(false), outer(_outer) {

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Address") {
      address = std::get<std::string>(v);
      channel = node_graph->get_channel(address);
      stub = ophanim_grpc::Ophanim::NewStub(channel);
      // channel = node_graph->get_channel(value);
    } else if (key_str == "Running") {
      if (!channel) {
        return;
      }

      running = std::get<bool>(v);
      grpc::ClientContext context;
      ophanim_grpc::Empty response;
      if (running) {
        stub->play_all(&context, ophanim_grpc::Empty(), &response);
        return;
      } else {
        stub->stop_all(&context, ophanim_grpc::Empty(), &response);
        return;
      }
    }
  }
};

OphanimNode::OphanimNode(ObservableDictPtr state,
                         boost::asio::io_context &io_context,
                         NodeGraph *node_graph)
    : impl(new Impl(state, io_context, node_graph, this)) {}

OphanimNode::~OphanimNode() {}

std::string OphanimNode::type_name() { return "OPHANIM"; }

size_t OphanimNode::modalities() const {
  return infer_modalities<OphanimNode>();
}
