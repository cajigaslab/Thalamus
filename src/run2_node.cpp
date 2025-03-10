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

using namespace thalamus;

struct Run2Node::Impl {
  boost::asio::io_context& io_context;
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
  std::map<std::string, std::string> redirects;

  Impl(ObservableDictPtr _state, boost::asio::io_context& io_context, NodeGraph *_graph)
      : state(_state), io_context(io_context), graph(_graph) {
    nodes = static_cast<ObservableList *>(state->parent);
    using namespace std::placeholders;
    state_connection =
        state->recursive_changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  void set_remote(std::string node, std::string address, bool value) {
    auto stub = graph->get_thalamus_stub(address);

    auto context = std::make_unique<grpc::ClientContext>();
    std::unique_ptr<thalamus_grpc::Empty> response = std::make_unique<thalamus_grpc::Empty>();
    std::unique_ptr<thalamus_grpc::ObservableTransaction> request = std::make_unique<thalamus_grpc::ObservableTransaction>();

    auto change = request->add_changes();
    auto node_address = absl::StrFormat("$.nodes[?@.name == '%s'].Running", absl::CEscape(node));
    change->set_address(node_address);
    change->set_value(value);
    change->set_action(thalamus_grpc::ObservableChange_Action::ObservableChange_Action_Set);

    stub->async()->observable_bridge_write(context.get(), request.get(), response.get(),
        [address,node_address,value,context=std::move(context),request=std::move(request),response=std::move(response)](grpc::Status s) mutable {
          THALAMUS_LOG(info) << absl::StrFormat("address: %s, request: %s, value: %s, status: %s", address, node_address, value, s.error_message());
        });
  }

  void get_redirect(std::string node, std::string address, bool value) {
    if(redirects.contains(address)) {
      set_remote(node, redirects.at(address), value);
    }

    auto stub = graph->get_thalamus_stub(address);

    auto context = std::make_unique<grpc::ClientContext>();
    std::unique_ptr<thalamus_grpc::Redirect> response;
    std::unique_ptr<thalamus_grpc::Empty> request;
    stub->async()->get_redirect(context.get(), request.get(), response.get(),
    [&,stub,node,address,value,context=std::move(context),request=std::move(request),response=std::move(response)](Status s) mutable {
      boost::asio::post(io_context, [&,node,address,value,stub,redirect=response.redirect()] {
        redirects[address] = redirect;
        set_remote(node, address, value);
      });
    });
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
          get_redirect(name, address);
        }

        auto context = std::make_unique<grpc::ClientContext>();
        thalamus_grpc::Thalamus::Stub* stub = nullptr;
        if(!address.empty()) {
          stub = graph->get_thalamus_stub(address);
        }

        auto do_run = [] {
        }

        if(stub) {
          if(!redirects.contains()) {
          }
        } else {
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
