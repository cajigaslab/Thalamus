#pragma once

#include <thalamus/base_node.hpp>

namespace thalamus {
  namespace node {
    boost::signals2::connection connect_ready_multithreaded(Node*, std::function<void(Node*)>);
    boost::signals2::connection connect_ready_singlethreaded(Node*, std::function<void(Node*)>);
    void signal_ready_offmain(Node*, boost::asio::io_context&);
    void signal_ready_onmain(Node*);
  }
}
