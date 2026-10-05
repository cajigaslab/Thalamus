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
    }

    void start() {
      get_node();
    }

    ~NodeSession() override {
      THALAMUS_LOG(trace) << "Delete NodeSession";
      start_join();
    }

    void start_join() {
      THALAMUS_LOG(trace) << "start_join";
      std::lock_guard<std::mutex> lock(state->mutex);
      timer.cancel();
      state->joining = true;
    }

    void get_node() {
      //THALAMUS_LOG(trace) << "getting node";
      boost::asio::post(io_context, [&,c_state=state] {
        std::lock_guard<std::mutex> lock(c_state->mutex);
        if(c_state->joining) {
          THALAMUS_LOG(trace) << "post joined";
          return;
        }

        get_node_connection = node_graph.get_node_scoped(selector, [&,c2_state=c_state](auto ptr) {
          std::lock_guard<std::mutex> lock2(c2_state->mutex);
          if(c2_state->joining) {
            THALAMUS_LOG(trace) << "get_node_connection joined";
            return;
          }

          weak_raw_node = ptr;
          raw_node = ptr.lock();
          typed_node = node_cast<NODE *>(raw_node.get());
          if (!typed_node) {
            timer.expires_after(1s);
            timer.async_wait(std::bind(&NodeSession<NODE, RESPONSE>::on_timer_get_node, this, _1, c2_state));
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
      timer.async_wait(std::bind(&NodeSession<NODE, RESPONSE>::on_timer_check_expired, this, _1, state));
    }

    void on_timer_get_node(const boost::system::error_code &error, std::shared_ptr<State> c_state) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      THALAMUS_ASSERT(!error, "Unexpected error");

      std::lock_guard<std::mutex> lock(c_state->mutex);
      if(c_state->joining) {
        return;
      }

      get_node();
    }

    void on_timer_check_expired(const boost::system::error_code &error, std::shared_ptr<State> c_state) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      THALAMUS_ASSERT(!error, "Unexpected error");

      std::lock_guard<std::mutex> lock(c_state->mutex);
      if(c_state->joining) {
        return;
      }

      if(weak_raw_node.lock() == nullptr) {
        THALAMUS_LOG(trace) << "node expired";
        timer.expires_after(1s);
        timer.async_wait(std::bind(&NodeSession<NODE, RESPONSE>::on_timer_get_node, this, _1, c_state));
      } else {
        timer.expires_after(1s);
        timer.async_wait(std::bind(&NodeSession<NODE, RESPONSE>::on_timer_check_expired, this, _1, c_state));
      }
    }
  };

  template <typename NODE, typename REQUEST>
  struct NodeReadSession : public ServerReadReactor<REQUEST> {
    NodeGraph& node_graph;
    boost::asio::io_context& io_context;
    boost::asio::steady_timer timer;
    boost::signals2::scoped_connection get_node_connection;
    thalamus_grpc::NodeSelector selector;
    std::weak_ptr<Node> weak_raw_node;
    std::shared_ptr<Node> raw_node;
    NODE* typed_node;
    ContextGuard context_guard;

    NodeReadSession(NodeGraph& _node_graph, boost::asio::io_context& _io_context,
                  ::grpc::CallbackServerContext& server_context,
                  ContextGuard&& _context_guard)
    : ServerReadReactor<REQUEST>(server_context, _io_context)
    , node_graph(_node_graph)
    , io_context(_io_context)
    , timer(_io_context)
    , context_guard(std::move(_context_guard)) {
      THALAMUS_LOG(trace) << "Create NodeReadSession";
    }

    ~NodeReadSession() override {
      THALAMUS_LOG(trace) << "Delete NodeReadSession";
      start_join();
    }

    void start_join(std::function<void()> cleanup = nullptr) override {
      ServerReadReactor<REQUEST>::start_join([&] {
        cleanup();
        timer.cancel();
      });
    }

    void set_selector(const thalamus_grpc::NodeSelector& new_selector) {
      auto state = ServerReadReactor<REQUEST>::state;
      boost::asio::post(io_context, [&,c_state=state,new_selector] {
        std::lock_guard<std::mutex> lock(c_state->mutex);
        if(c_state->joining) {
          THALAMUS_LOG(trace) << "get_node_connection joined";
          return;
        }
        this->selector = new_selector;
        get_node();
      });
    }

    void get_node() {
      //THALAMUS_LOG(trace) << "getting node";
      auto state = ServerReadReactor<REQUEST>::state;
      boost::asio::post(io_context, [&,c_state=state] {
        std::lock_guard<std::mutex> lock(c_state->mutex);
        if(c_state->joining) {
          THALAMUS_LOG(trace) << "get_node_connection joined";
          return;
        }

        get_node_connection = node_graph.get_node_scoped(selector, [&,c2_state=c_state](auto ptr) {
          std::lock_guard<std::mutex> lock2(c2_state->mutex);
          if(c2_state->joining) {
            THALAMUS_LOG(trace) << "get_node_connection joined";
            return;
          }

          weak_raw_node = ptr;
          raw_node = ptr.lock();
          typed_node = node_cast<NODE *>(raw_node.get());
          if (!typed_node) {
            timer.expires_after(1s);
            timer.async_wait(std::bind(&NodeReadSession<NODE, REQUEST>::on_timer_get_node, this, _1, c2_state));
            return;
          }
          
          THALAMUS_LOG(trace) << "got node";
          raw_node.reset();

          timer.expires_after(1s);
          timer.async_wait(std::bind(&NodeReadSession<NODE, REQUEST>::on_timer_check_expired, this, _1, c2_state));
          on_node();
        });
      });
    }

    std::shared_ptr<Node> lock() {
      return typed_node ? weak_raw_node.lock() : std::shared_ptr<Node>();
    }

    virtual void on_node() {}

    void on_timer_get_node(const boost::system::error_code &error, std::shared_ptr<typename ServerReadReactor<REQUEST>::State> c_state) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      THALAMUS_ASSERT(!error, "Unexpected error");

      std::lock_guard<std::mutex> lock(c_state->mutex);
      if(c_state->joining) {
        return;
      }

      get_node();
    }

    void on_timer_check_expired(const boost::system::error_code &error, std::shared_ptr<typename ServerReadReactor<REQUEST>::State> c_state) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      THALAMUS_ASSERT(!error, "Unexpected error");

      std::lock_guard<std::mutex> lock(c_state->mutex);
      if(c_state->joining) {
        return;
      }

      if(weak_raw_node.lock() == nullptr) {
        THALAMUS_LOG(trace) << "node expired";
        timer.expires_after(1s);
        timer.async_wait(std::bind(&NodeReadSession<NODE, REQUEST>::on_timer_get_node, this, _1, c_state));
      } else {
        timer.expires_after(1s);
        timer.async_wait(std::bind(&NodeReadSession<NODE, REQUEST>::on_timer_check_expired, this, _1, c_state));
      }
    }
  };

  template <typename NODE, typename REQUEST, typename RESPONSE>
  struct NodeBidiSession : public ServerBidiReactor2<REQUEST, RESPONSE> {
    NodeGraph& node_graph;
    boost::asio::io_context& io_context;
    boost::asio::steady_timer timer;
    boost::signals2::scoped_connection get_node_connection;
    thalamus_grpc::NodeSelector selector;
    std::weak_ptr<Node> weak_raw_node;
    std::shared_ptr<Node> raw_node;
    NODE* typed_node;
    ContextGuard context_guard;

    NodeBidiSession(NodeGraph& _node_graph, boost::asio::io_context& _io_context,
                  ::grpc::CallbackServerContext& server_context,
                  ContextGuard&& _context_guard)
    : ServerBidiReactor2<REQUEST, RESPONSE>(server_context, _io_context)
    , node_graph(_node_graph)
    , io_context(_io_context)
    , timer(_io_context)
    , context_guard(std::move(_context_guard)) {
      THALAMUS_LOG(trace) << "Create NodeBidiSession";
    }

    ~NodeBidiSession() override {
      THALAMUS_LOG(trace) << "Delete NodeBidiSession";
      start_join();
    }

    void start_join(std::function<void()> cleanup = nullptr) override {
      ServerBidiReactor2<REQUEST, RESPONSE>::start_join([&] {
        cleanup();
        timer.cancel();
      });
    }

    void set_selector(const thalamus_grpc::NodeSelector& new_selector) {
      auto state = ServerBidiReactor2<REQUEST, RESPONSE>::state;
      boost::asio::post(io_context, [&,c_state=state,new_selector] {
        std::lock_guard<std::mutex> lock(c_state->mutex);
        if(c_state->joining) {
          THALAMUS_LOG(trace) << "get_node_connection joined";
          return;
        }
        this->selector = new_selector;
        get_node();
      });
    }

    void get_node() {
      //THALAMUS_LOG(trace) << "getting node";
      auto state = ServerBidiReactor2<REQUEST, RESPONSE>::state;
      boost::asio::post(io_context, [&,c_state=state] {
        std::lock_guard<std::mutex> lock(c_state->mutex);
        if(c_state->joining) {
          THALAMUS_LOG(trace) << "get_node_connection joined";
          return;
        }

        get_node_connection = node_graph.get_node_scoped(selector, [&,c2_state=c_state](auto ptr) {
          std::unique_lock<std::mutex> lock2(c2_state->mutex);
          if(c2_state->joining) {
            THALAMUS_LOG(trace) << "get_node_connection joined";
            return;
          }

          weak_raw_node = ptr;
          raw_node = ptr.lock();
          typed_node = node_cast<NODE *>(raw_node.get());
          if (!typed_node) {
            timer.expires_after(1s);
            timer.async_wait(std::bind(&NodeBidiSession<NODE, REQUEST, RESPONSE>::on_timer_get_node, this, _1, c2_state));
            return;
          }
          
          THALAMUS_LOG(trace) << "got node";
          raw_node.reset();

          timer.expires_after(1s);
          timer.async_wait(std::bind(&NodeBidiSession<NODE, REQUEST, RESPONSE>::on_timer_check_expired, this, _1, c2_state));
          on_node(lock2);
        });
      });
    }

    std::shared_ptr<Node> lock() {
      return typed_node ? weak_raw_node.lock() : std::shared_ptr<Node>();
    }

    virtual void on_node(std::unique_lock<std::mutex>&) {}

    void on_timer_get_node(const boost::system::error_code &error, std::shared_ptr<typename ServerBidiReactor2<REQUEST, RESPONSE>::State> c_state) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      THALAMUS_ASSERT(!error, "Unexpected error");

      std::lock_guard<std::mutex> lock(c_state->mutex);
      if(c_state->joining) {
        return;
      }

      get_node();
    }

    void on_timer_check_expired(const boost::system::error_code &error, std::shared_ptr<typename ServerBidiReactor2<REQUEST, RESPONSE>::State> c_state) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      THALAMUS_ASSERT(!error, "Unexpected error");

      std::lock_guard<std::mutex> lock(c_state->mutex);
      if(c_state->joining) {
        return;
      }

      if(weak_raw_node.lock() == nullptr) {
        THALAMUS_LOG(trace) << "node expired";
        timer.expires_after(1s);
        timer.async_wait(std::bind(&NodeBidiSession<NODE, REQUEST, RESPONSE>::on_timer_get_node, this, _1, c_state));
      } else {
        timer.expires_after(1s);
        timer.async_wait(std::bind(&NodeBidiSession<NODE, REQUEST, RESPONSE>::on_timer_check_expired, this, _1, c_state));
      }
    }
  };
}
