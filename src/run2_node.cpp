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

#include <absl/strings/escaping.h>
#include <absl/strings/str_replace.h>

using namespace thalamus;

struct Run2Node::Impl {
  ObservableDictPtr state;
  boost::asio::io_context& io_context;
  ObservableListPtr targets_state = nullptr;
  ObservableList *nodes;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection nodes_state_connection;
  NodeGraph *graph;
  struct Target {
    thalamus_grpc::Thalamus::Stub* stub;
  };
  std::map<std::string, std::unique_ptr<thalamus_grpc::Thalamus::Stub>> stubs;
  std::map<std::string, std::string> redirects;

  Impl(ObservableDictPtr _state, boost::asio::io_context& _io_context, NodeGraph *_graph)
      : state(_state), io_context(_io_context), graph(_graph) {
    nodes = static_cast<ObservableList *>(state->parent);
    using namespace std::placeholders;
    state_connection =
        state->recursive_changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  void set_remote(std::string node, std::string address, bool value) {
    address = redirects[address];
    auto stub = graph->get_thalamus_stub(address);

    auto context = std::make_shared<grpc::ClientContext>();
    auto request = std::make_shared<thalamus_grpc::ObservableTransaction>();
    auto response = std::make_shared<thalamus_grpc::Empty>();

    auto change = request->add_changes();
    auto node_address = absl::StrFormat("$.nodes[?@.name == '%s'].Running", absl::CEscape(node));
    change->set_address(node_address);
    change->set_value(value ? "true" : "false");
    change->set_action(thalamus_grpc::ObservableChange_Action::ObservableChange_Action_Set);

    stub->async()->observable_bridge_write(context.get(), request.get(), response.get(),
        [address,node_address,value,moved_context=context,moved_request=request,moved_response=response](grpc::Status s) {
          THALAMUS_LOG(info) << absl::StrFormat("address: %s, request: %s, value: %d, status: %s", address, node_address, value, s.error_message());
        });
  }

  void get_redirect(std::string node, std::string address, bool value) {
    if(redirects.contains(address)) {
      set_remote(node, redirects.at(address), value);
    }

    auto stub = graph->get_thalamus_stub(address);

    auto context = std::make_shared<grpc::ClientContext>();
    auto request = std::make_shared<thalamus_grpc::Empty>();
    auto response = std::make_shared<thalamus_grpc::Redirect>();
    stub->async()->get_redirect(context.get(), request.get(), response.get(),
    [this,node,address,value,moved_context=context,moved_request=request,moved_response=response](grpc::Status s) {
      THALAMUS_LOG(info) << absl::StrFormat("redirect status, address: %s, redirect: %s, status: %s", address, moved_response->redirect(), s.error_message());
      if(!s.ok()) {
        THALAMUS_LOG(warning) << "Aborting";
        return;
      }
      boost::asio::post(io_context, [&,node,address,value,redirect=moved_response->redirect()] {
        std::vector<std::string> address_tokens = absl::StrSplit(address, ':');
        auto processed = absl::StrReplaceAll(redirect, {{"localhost", address_tokens.front()}});
        redirects[address] = redirect.empty() ? address : processed;
        set_remote(node, address, value);
      });
    });
  }

  void on_change(ObservableCollection* source,
                 ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    if(source != state.get()) {
      return;
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
        std::string name =
            target->contains("Name") ? target->at("Name") : std::string();
        std::string address = target->contains("Address")
                                  ?  target->at("Address") : std::string();

        if(address.empty()) {
          for(size_t j = 0;j < nodes->size();++j) {
            ObservableDictPtr node = nodes->at(j);
            if(node->contains("name")) {
              std::string node_name = node->at("name");
              if(name == node_name) {
                node->at("Running").assign(value_bool, [&] {});
              }
            }
          }
          continue;
        }

        if(!redirects.contains(address)) {
          get_redirect(name, address, value_bool);
        } else {
          set_remote(name, address, value_bool);
        }
      }
    }
  }
};

Run2Node::Run2Node(ObservableDictPtr state, boost::asio::io_context &io_context,
                 NodeGraph *graph)
    : impl(new Impl(state, io_context, graph)) {}

Run2Node::~Run2Node() {}

std::string Run2Node::type_name() { return "RUNNER2"; }
