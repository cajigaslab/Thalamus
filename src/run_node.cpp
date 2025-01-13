#include <run_node.hpp>
#include <util.hpp>
#include <grpcpp/create_channel.h>
#include <thalamus.grpc.pb.h>

using namespace thalamus;

struct RunNode::Impl {
    ObservableDictPtr state;
    ObservableList* nodes;
    boost::signals2::scoped_connection state_connection;
    boost::signals2::scoped_connection nodes_state_connection;
    NodeGraph* graph;

    Impl(ObservableDictPtr state, NodeGraph* graph)
      : state(state)
      , graph(graph) {
      nodes = static_cast<ObservableList*>(state->parent);
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Running") {
        if(!state->contains("Targets")) {
          return;
        }
        std::string value_str = state->at("Targets");
        auto tokens = absl::StrSplit(value_str, ',');
        auto targets = get_nodes(nodes, tokens);

        auto value_bool = std::get<bool>(v);
        for (auto target : targets) {
          auto locked = target.lock();
          if(locked == state) {
            continue;
          }
          locked->at("Running").assign(value_bool, [&] {});
        }
      }
    }
  };

  RunNode::RunNode(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph)
      : impl(new Impl(state, graph)) {}

  RunNode::~RunNode() {}

  std::string RunNode::type_name() {
    return "RUNNER";
  }
