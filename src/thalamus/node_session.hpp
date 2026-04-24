#pragma once
#include <thalamus/tracing.hpp>

#include <thalamus/base_node.hpp>
#include <chrono>
#include <thalamus/state.hpp>
#include <thalamus/util.hpp>
#include <thalamus/xsens_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/signals2.hpp>
#include <thalamus.grpc.pb.h>
#include <boost/qvm/quat_access.hpp>
#include <boost/qvm/vec_access.hpp>
#include <grpcpp/support/status.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <thalamus/grpc_impl.hpp>
#include <thalamus/image_node.hpp>
#include <thalamus/modalities_util.hpp>
#include <thalamus/text_node.hpp>
#include <thalamus/thread.hpp>
#include <thalamus/grpc.hpp>

namespace thalamus {
  class ContextGuard {
  public:
    Service *service;
    ::grpc::ServerContextBase *context;
    ContextGuard(Service *_service, ::grpc::ServerContextBase *_context);
    ContextGuard(const ContextGuard&) = delete;
    ContextGuard(ContextGuard&&);
    ~ContextGuard();
  };

  template <typename NODE, typename RESPONSE>
  struct NodeSession : public ServerWriteReactor<RESPONSE> {
    struct State {
      std::mutex mutex;
      bool joining = false;
      ~State() {
        THALAMUS_LOG(trace) << "Delete State";
      }
    };
    std::shared_ptr<State> state = std::make_shared<State>();

    NodeGraph& node_graph;
    boost::asio::io_context& io_context;
    boost::asio::steady_timer timer;
    boost::signals2::scoped_connection get_node_connection;
    const thalamus_grpc::NodeSelector selector;
    std::weak_ptr<Node> weak_raw_node;
    std::shared_ptr<Node> raw_node;
    NODE* typed_node;
    ContextGuard context_guard;

    NodeSession(NodeGraph& _node_graph, boost::asio::io_context& _io_context,
                  ::grpc::CallbackServerContext& server_context, const thalamus_grpc::NodeSelector& _selector,
                  ContextGuard&& _context_guard)
    : ServerWriteReactor<RESPONSE>(server_context)
    , node_graph(_node_graph)
    , io_context(_io_context)
    , timer(_io_context)
    , selector(_selector)
    , context_guard(std::move(_context_guard)) {
      THALAMUS_LOG(trace) << "Create NodeSession";

      get_node();
    }

    ~NodeSession() override {
      THALAMUS_LOG(trace) << "Delete NodeSession";
      std::lock_guard<std::mutex> lock(state->mutex);
      state->joining = true;
    }
        
    void OnDone() override {
      THALAMUS_LOG(trace) << "OnDone" << std::endl;
      ServerWriteReactor<RESPONSE>::OnDone();
      delete this;
    }

    void OnCancel() override {
      THALAMUS_LOG(trace) << "OnCancel" << std::endl;
      grpc::ServerWriteReactor<RESPONSE>::Finish(grpc::Status::OK);
      //delete this;
    }

    void get_node() {
      //THALAMUS_LOG(trace) << "getting node";
      boost::asio::post(io_context, [&] {
        get_node_connection = node_graph.get_node_scoped(selector, [&,c_state=state](auto ptr) {
          std::lock_guard<std::mutex> lock(c_state->mutex);
          if(c_state->joining) {
            THALAMUS_LOG(trace) << "get_node_connection joined";
            return;
          }

          weak_raw_node = ptr;
          raw_node = ptr.lock();
          typed_node = node_cast<NODE *>(raw_node.get());
          if (!typed_node) {
            timer.expires_after(1s);
            timer.async_wait(std::bind(&NodeSession<NODE, RESPONSE>::on_timer_get_node, this, _1));
            return;
          }
          on_node();
        });
      });
    }

    virtual void subscribe() = 0;

    void on_node() {
      THALAMUS_LOG(trace) << "got node";
      subscribe();
      raw_node.reset();

      timer.expires_after(1s);
      timer.async_wait(std::bind(&NodeSession<NODE, RESPONSE>::on_timer_check_expired, this, _1));
    }

    void on_timer_get_node(const boost::system::error_code &error) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      THALAMUS_ASSERT(!error, "Unexpected error");

      get_node();
    }

    void on_timer_check_expired(const boost::system::error_code &error) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      THALAMUS_ASSERT(!error, "Unexpected error");

      if(weak_raw_node.lock() == nullptr) {
        THALAMUS_LOG(trace) << "node expired";
        timer.expires_after(1s);
        timer.async_wait(std::bind(&NodeSession<NODE, RESPONSE>::on_timer_get_node, this, _1));
      } else {
        timer.expires_after(1s);
        timer.async_wait(std::bind(&NodeSession<NODE, RESPONSE>::on_timer_check_expired, this, _1));
      }
    }
  };
}
