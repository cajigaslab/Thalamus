#include <log_node.h>

namespace thalamus {
  struct LogNode::Impl {
    std::string_view text;
    std::chrono::nanoseconds time;
  };
  LogNode::LogNode(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph) : impl(new Impl()) {}
  LogNode::~LogNode() {}
  std::string_view LogNode::text() const {
    return impl->text;
  }
  bool LogNode::has_text_data() const {
    return true;
  }
  boost::json::value LogNode::process(const boost::json::value& value) {
    auto text = value.as_string();
    impl->text = std::string_view(text.begin(), text.end());
    impl->time = std::chrono::steady_clock::now().time_since_epoch();
    ready(this); 
    return boost::json::value();
  }
  std::chrono::nanoseconds LogNode::time() const {
    return impl->time;
  }
  std::string LogNode::type_name() {
    return "LOG";
  }
}
