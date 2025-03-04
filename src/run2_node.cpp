#include <run2_node.hpp>
#include <util.hpp>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <grpcpp/create_channel.h>
#include <thalamus.grpc.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace thalamus;

struct Run2Node::Impl {
  ObservableDictPtr state;
  ObservableListPtr targets_state = nullptr;
  ObservableList *nodes;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection nodes_state_connection;
  NodeGraph *graph;
  struct Target {
    thalamus_grpc::Thalamus::Stub* stub;
  };
  std::map<std::string, std::unique_ptr<thalamus_grpc::Thalamus::Stub>> stubs;
  std::map<std::string, Target> targets;

  Impl(ObservableDictPtr _state, NodeGraph *_graph)
      : state(_state), graph(_graph) {
    nodes = static_cast<ObservableList *>(state->parent);
    using namespace std::placeholders;
    state_connection =
        state->recursive_changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  void on_change(ObservableCollection* source,
                 ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    if(source == state.get()) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Targets") {
        targets_state = std::get<ObservableListPtr>(v);
        targets_state->recap(std::bind(&Impl::on_change, this, targets_state.get(), _1, _2, _3));
      }
    } else if(source == targets_state.get()) {
      for(size_t i = 0;i < targets_state->size();++i) {
        ObservableDictPtr v2 = targets_state->at(i);
        targets_state->recap(std::bind(&Impl::on_change, this, v2.get(), _1, _2, _3));
      }
      targets_state->recap(std::bind(&Impl::on_change, this, targets_state.get(), _1, _2, _3));
    } else if(source && source->parent == targets_state.get()) {
      auto index = targets_state->key_of(*source);

    }
    auto key_str = std::get<std::string>(k);
    if (key_str == "Running") {
      auto value_bool = std::get<bool>(v);
      if (!state->contains("Targets")) {
        return;
      }
      ObservableListPtr targets = state->at("Targets");
      for(size_t i = 0;i < targets->size();++i) {
        ObservableDictPtr target = targets->at(i);
        std::string name = target->contains("Name") ? std::string() : target->at("Name");
        std::string address = target->contains("Address") ? std::string() : target->at("Address");

        thalamus_grpc::Thalamus::Stub* stub = nullptr;
        if(!address.empty()) {
          stub = graph->get_thalamus_stub(address);
        }

        if(stub) {
        } else {
          for(size_t i = 0;i < nodes->size();++i) {
            ObservableDictPtr node = nodes->at(i);
            if(node->contains("name")) {
              std::string node_name = node->at("name");
              if(name == node_name) {
                node->at("Running").assign(value_bool, [&] {});
              }
            }
          }
        }
      }
    }
  }
};

Run2Node::Run2Node(ObservableDictPtr state, boost::asio::io_context &,
                 NodeGraph *graph)
    : impl(new Impl(state, graph)) {}

Run2Node::~Run2Node() {}

std::string Run2Node::type_name() { return "RUNNER2"; }
