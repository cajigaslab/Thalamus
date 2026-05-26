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
    boost::signals2::connection connect_ready_multithreaded(Node* node, std::function<void(Node*)> callback) {
      if(node->ready_multithreaded) {
        return node->ready_multithreaded->connect(callback);
      }
      return node->ready.connect(callback);
    }

    boost::signals2::connection connect_ready_singlethreaded(Node* node, std::function<void(Node*)> callback) {
      return node->ready.connect(callback);
    }

    void signal_ready(Node* node, boost::asio::io_context& context) {
      std::optional<std::future<void>> future;
      if(!node->ready.empty()) {
        future = boost::asio::post(context, boost::asio::use_future([node] {
          node->ready(node);
        }));
      }

      if(node->ready_multithreaded) {
        (*node->ready_multithreaded)(node);
      }
      
      if(future.has_value()) {
        future->get();
      }
    }
  }
}
