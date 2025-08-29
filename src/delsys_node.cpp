#include <delsys_node.hpp>
#include <modalities_util.hpp>

#include <thalamus/grpc.hpp>

namespace thalamus {
struct DelsysNode::Impl {
  std::string location;
  boost::asio::io_context& io_context;
  ObservableDictPtr state;
  DelsysNode * outer;
  NodeGraph* graph;
  boost::signals2::scoped_connection state_connection;
  thalamus_grpc::AnalogRequest request;
  thalamus_grpc::TextRequest text_request;
  std::unique_ptr<ReadReactor<thalamus_grpc::AnalogResponse>> reactor;
  std::unique_ptr<ReadReactor<thalamus_grpc::Text>> text_reactor;
  std::unique_ptr<BidiReactor<thalamus_grpc::NodeRequest, thalamus_grpc::NodeResponse>> request_reactor;
  std::vector<double> data;
  std::vector<std::span<const double>> spans;
  std::vector<std::string> names;
  std::vector<std::chrono::nanoseconds> sample_intervals;
  bool has_analog_data = false;
  bool has_text_data = false;
  bool response_received = false;
  std::chrono::nanoseconds now;
  std::string text;
  std::map<uint64_t, std::function<void(const boost::json::value &)>> callbacks;
  uint64_t next_id = 1;
  
  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       DelsysNode *_outer, NodeGraph *_graph)
      : io_context(_io_context), state(_state), outer(_outer), graph(_graph) {
    using namespace std::placeholders;
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    boost::asio::co_spawn(io_context, loop(), boost::asio::detached);
  }

  ~Impl() {

  }

  void on_analog(const thalamus_grpc::AnalogResponse& response) {
    data.assign(response.data().begin(), response.data().end());
    spans.resize(size_t(response.spans().size()));
    names.resize(size_t(response.spans().size()));
    sample_intervals.resize(size_t(response.spans().size()));
    std::transform(response.spans().begin(), response.spans().end(), spans.begin(), [&](auto arg) {
      return std::span<const double>(data.begin() + arg.begin(), data.begin() + arg.end());
    });
    std::transform(response.spans().begin(), response.spans().end(), names.begin(), [](auto arg) {
      return arg.name();
    });
    std::transform(response.sample_intervals().begin(), response.sample_intervals().end(), sample_intervals.begin(), [](auto arg) {
      return std::chrono::nanoseconds(arg);
    });
    now = std::chrono::nanoseconds(response.time());

    has_analog_data = true;
    has_text_data = false;
    if(response.channels_changed()) {
      outer->channels_changed(outer);
    }
    outer->ready(outer);
  }

  void on_text(const thalamus_grpc::Text& response) {
    text = response.text();
    now = std::chrono::nanoseconds(response.time());
    has_analog_data = false;
    has_text_data = true;
    outer->ready(outer);
  }

  void update_stream(bool reset) {
    auto has_connections = !outer->ready.empty();
    if(has_connections && (!reactor || reset)) {
      reactor.reset(new ReadReactor<thalamus_grpc::AnalogResponse>(io_context, std::bind(&Impl::on_analog, this, _1)));
      text_reactor.reset(new ReadReactor<thalamus_grpc::Text>(io_context, std::bind(&Impl::on_text, this, _1)));

      auto stub = graph->get_thalamus_stub(location);
      auto selector = request.mutable_node();
      std::string name = state->at("name");
      selector->set_name(name);
      stub->async()->analog(&reactor->context, &request, reactor.get());

      *text_request.mutable_node() = *selector;
      stub->async()->text(&text_reactor->context, &text_request, text_reactor.get());

      reactor->start();
      text_reactor->start();
    } else if(!has_connections) {
      reactor.reset();
      text_reactor.reset();
    }
  }

  void update_request_stream(bool reset) {
    if(!request_reactor || reset) {
      request_reactor.reset(new BidiReactor<thalamus_grpc::NodeRequest, thalamus_grpc::NodeResponse>(io_context, [&] (auto response) {
        response_received = true;
        auto parsed = boost::json::parse(response.json());
        auto i = callbacks.find(response.id());
        auto callback = i->second;
        callbacks.erase(i);
        callback(parsed);
      }));

      auto stub = graph->get_thalamus_stub(location);
      response_received = false;
      stub->async()->node_request_stream(&request_reactor->context, request_reactor.get());

      request_reactor->start();
    }

    if(!response_received) {
      thalamus_grpc::NodeRequest node_request;
      std::string name = state->at("name");
      node_request.set_node(name);
      request_reactor->send(std::move(node_request));
    }
  }

  boost::asio::awaitable<void> loop() {
    boost::asio::steady_timer timer(io_context);
    while(true) {
      timer.expires_after(1s);
      auto [ec] = co_await timer.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));
      if(ec) {
        THALAMUS_LOG(info) << "DELSYS loop exit";
        co_return;
      }
      update_stream(false);
    }
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "name") {
      response_received = false;
    } else if (key_str == "Location") {
      location = std::get<std::string>(v);
      if(!location.empty()) {
        update_stream(true);
      }
      //outer->channels_changed(outer);
      //outer->ready(outer);
    }
  }
};

DelsysNode::DelsysNode(ObservableDictPtr state, boost::asio::io_context & io_context, NodeGraph * graph)
    : impl(new Impl(state, io_context, this, graph)) {

}

DelsysNode::~DelsysNode() {}

void DelsysNode::inject(const thalamus::vector<std::span<double const>> &,
            const thalamus::vector<std::chrono::nanoseconds> &,
            const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

std::string_view DelsysNode::name(int i) const {
  return impl->names[size_t(i)];
}

int DelsysNode::num_channels() const {
  return int(impl->names.size());
}
std::chrono::nanoseconds DelsysNode::sample_interval(int i) const {
  return impl->sample_intervals[size_t(i)];
}
std::chrono::nanoseconds DelsysNode::time() const {
  return impl->now;
}
std::span<const double> DelsysNode::data(int i) const {
  return impl->spans[size_t(i)];
}

std::string DelsysNode::type_name() { return "DELSYS"; }
size_t DelsysNode::modalities() const { return infer_modalities<DelsysNode>(); }

std::string_view DelsysNode::redirect() const {
  return "";
}

std::string_view DelsysNode::text() const {
  return impl->text;
}

bool DelsysNode::has_text_data() const {
  return impl->has_text_data;
}

bool DelsysNode::has_analog_data() const {
  return impl->has_analog_data;
}

void DelsysNode::process(const boost::json::value & request, std::function<void(const boost::json::value &)> callback) {
  impl->update_request_stream(false);
  auto serialized_request = boost::json::serialize(request);
  thalamus_grpc::NodeRequest sub_request;
  sub_request.set_json(serialized_request);
  sub_request.set_id(impl->next_id++);
  impl->callbacks[sub_request.id()] = callback;
  impl->request_reactor->send(std::move(sub_request));
}

} // namespace thalamus
