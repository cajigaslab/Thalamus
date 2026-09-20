#include <optional>
#include <thalamus/node_util.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "boost/asio/use_future.hpp"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
  namespace node {
    
    struct OffMainSignaler::Impl {
      Node& node;
      boost::asio::io_context& io_context;
      bool blocked;
      std::mutex mutex;
      std::condition_variable cv;
      Impl(Node& _node, boost::asio::io_context& _io_context)
      : node(_node)
      , io_context(_io_context)
      , blocked(true) {}
    };

    OffMainSignaler::OffMainSignaler(Node& _node, boost::asio::io_context& _io_context)
    : impl(new Impl(_node, _io_context)) {} 
      
    void OffMainSignaler::block() {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->blocked = true;
      impl->cv.notify_one();
    }

    void OffMainSignaler::unblock() {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->blocked = false;
      impl->cv.notify_one();
    }

    bool OffMainSignaler::signal() {
      std::unique_lock<std::mutex> lock(impl->mutex);
      if(impl->blocked) {
        return false;
      }

      auto done = false;
      auto post_to_main = !impl->node.ready.empty();
      if(post_to_main) {
        boost::asio::post(impl->io_context, [_impl=this->impl,&done] {
          std::lock_guard<std::mutex> lock2(_impl->mutex);
          if(_impl->blocked) {
            return;
          }
          _impl->node.ready(&_impl->node);
          done = true;
          _impl->cv.notify_one();
        });
      }

      if(impl->node.ready_multithreaded) {
        (*impl->node.ready_multithreaded)(&impl->node);
      }

      if(post_to_main) {
        impl->cv.wait(lock, [_impl=this->impl,&done] {
          return _impl->blocked || done;
        });
      }
      return !impl->blocked;
    }

    boost::signals2::connection connect_ready_multithreaded(Node* node, std::function<void(Node*)> callback) {
      if(node->ready_multithreaded) {
        return node->ready_multithreaded->connect(callback);
      }
      return node->ready.connect(callback);
    }

    boost::signals2::connection connect_ready_singlethreaded(Node* node, std::function<void(Node*)> callback) {
      return node->ready.connect(callback);
    }

    void signal_ready_offmain(Node* node, boost::asio::io_context& context) {
      std::optional<std::future<void>> future;
      if(!node->ready.empty()) {
        future = boost::asio::post(context, boost::asio::use_future([node] {
          node->ready(node);
        }));
      }

      if(node->ready_multithreaded) {
        (*node->ready_multithreaded)(node);
      }

      if(future) {
        future->get();
      }
    }

    void signal_ready_onmain(Node* node) {
      node->ready(node);
      if(node->ready_multithreaded) {
        (*node->ready_multithreaded)(node);
      }
    }
  }
}
