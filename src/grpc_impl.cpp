#include <algorithm>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/qvm/quat_access.hpp>
#include <boost/qvm/vec_access.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <grpc_impl.hpp>
#include <h5handle.hpp>
#include <image_node.hpp>
#include <modalities_util.hpp>
#include <text_node.hpp>
#include <thalamus/thread.hpp>
#include <thalamus/tracing.hpp>

namespace thalamus {
using namespace std::chrono_literals;
using namespace std::placeholders;

#define SWAP(a, b)                                                             \
  tempr = (a);                                                                 \
  (a) = (b);                                                                   \
  (b) = tempr
static void four1(double data[], unsigned long nn, int isign) {
  unsigned long n, mmax, m, j, istep, i;
  double wtemp, wr, wpr, wpi, wi, theta;
  double tempr, tempi;
  n = nn << 1;
  j = 1;
  for (i = 1; i < n; i += 2) {
    if (j > i) {
      SWAP(data[j], data[i]);
      SWAP(data[j + 1], data[i + 1]);
    }
    m = n >> 1;
    while (m >= 2 && j > m) {
      j -= m;
      m >>= 1;
    }
    j += m;
  }
  mmax = 2;
  while (n > mmax) {
    istep = mmax << 1;
    theta = isign * (6.28318530717959 / double(mmax));
    wtemp = sin(0.5 * theta);
    wpr = -2.0 * wtemp * wtemp;
    wpi = sin(theta);
    wr = 1.0;
    wi = 0.0;
    for (m = 1; m < mmax; m += 2) {
      for (i = m; i <= n; i += istep) {
        j = i + mmax;
        tempr = wr * data[j] - wi * data[j + 1];
        tempi = wr * data[j + 1] + wi * data[j];
        data[j] = data[i] - tempr;
        data[j + 1] = data[i + 1] - tempi;
        data[i] += tempr;
        data[i + 1] += tempi;
      }
      wr = (wtemp = wr) * wpr - wi * wpi + wr;
      wi = wi * wpr + wtemp * wpi + wi;
    }
    mmax = istep;
  }
}

static void realft(double data[], unsigned long n, int isign) {
  unsigned long i, i1, i2, i3, i4, np3;
  double c1 = 0.5, c2, h1r, h1i, h2r, h2i;
  double wr, wi, wpr, wpi, wtemp, theta;
  theta = 3.141592653589793 / double(n >> 1);
  if (isign == 1) {
    c2 = -0.5;
    four1(data, n >> 1, 1);
  } else {
    c2 = 0.5;
    theta = -theta;
  }
  wtemp = sin(0.5 * theta);
  wpr = -2.0 * wtemp * wtemp;
  wpi = sin(theta);
  wr = 1.0 + wpr;
  wi = wpi;
  np3 = n + 3;
  for (i = 2; i <= (n >> 2); i++) {
    i4 = 1 + (i3 = np3 - (i2 = 1 + (i1 = i + i - 1)));
    h1r = c1 * (data[i1] + data[i3]);
    h1i = c1 * (data[i2] - data[i4]);
    h2r = -c2 * (data[i2] + data[i4]);
    h2i = c2 * (data[i1] - data[i3]);
    data[i1] = h1r + wr * h2r - wi * h2i;
    data[i2] = h1i + wr * h2i + wi * h2r;
    data[i3] = h1r - wr * h2r + wi * h2i;
    data[i4] = -h1i + wr * h2i + wi * h2r;
    wr = (wtemp = wr) * wpr - wi * wpi + wr;
    wi = wi * wpr + wtemp * wpi + wi;
  }
  if (isign == 1) {
    data[1] = (h1r = data[1]) + data[2];
    data[2] = h1r - data[2];
  } else {
    data[1] = c1 * ((h1r = data[1]) + data[2]);
    data[2] = c1 * (h1r - data[2]);
    four1(data, n >> 1, -1);
  }
}

struct Service::Impl {
  ObservableCollection::Value state;
  ObservableCollection *root;
  boost::asio::io_context &io_context;
  std::atomic<::grpc::ServerReaderWriter<::thalamus_grpc::ObservableChange,
                                         ::thalamus_grpc::ObservableChange> *>
      observable_bridge_stream;
  std::atomic<::grpc::ServerReaderWriter<::thalamus_grpc::EvalRequest,
                                         ::thalamus_grpc::EvalResponse> *>
      eval_stream;
  std::atomic<::grpc::ServerReaderWriter<::thalamus_grpc::EvalRequest,
                                         ::thalamus_grpc::EvalResponse> *>
      graph_stream;
  std::atomic<::grpc::ServerWriter<::thalamus_grpc::Notification> *>
      notification_writer;
  std::map<unsigned long long, std::function<void()>> pending_changes;
  std::map<unsigned long long, std::promise<ObservableCollection::Value>>
      eval_promises;
  std::mutex mutex;
  std::set<::grpc::ServerContext *> contexts;
  std::set<std::promise<void> *> promises;
  std::condition_variable condition;
  NodeGraph &node_graph;
  Service *outer;
  std::string observable_bridge_redirect;
  std::vector<boost::signals2::scoped_connection> state_connections;
  std::vector<::grpc::internal::WriterInterface<
      ::thalamus_grpc::ObservableTransaction> *>
      observable_bridge_clients;
  std::map<std::string, ::grpc::internal::WriterInterface<
                            ::thalamus_grpc::ObservableTransaction> *>
      peer_name_to_observable_bridge_client;

  Impl(ObservableCollection::Value _state, boost::asio::io_context &_io_context,
       NodeGraph &_node_graph, std::string _observable_bridge_redirect,
       Service *_outer)
      : state(_state), root(nullptr), io_context(_io_context),
        observable_bridge_stream(nullptr), notification_writer(nullptr),
        node_graph(_node_graph), outer(_outer),
        observable_bridge_redirect(_observable_bridge_redirect) {

    if (std::holds_alternative<ObservableListPtr>(state)) {
      auto unwrapped = std::get<ObservableListPtr>(state);
      root = unwrapped.get();
      state_connections.push_back(unwrapped->changed.connect(
          std::bind(&Impl::on_change, this, root, _1, _2, _3)));
      unwrapped->recap(
          std::bind(&Impl::on_change, this, unwrapped.get(), _1, _2, _3));
    } else if (std::holds_alternative<ObservableDictPtr>(state)) {
      auto unwrapped = std::get<ObservableDictPtr>(state);
      root = unwrapped.get();
      state_connections.push_back(unwrapped->changed.connect(
          std::bind(&Impl::on_change, this, root, _1, _2, _3)));
      unwrapped->recap(
          std::bind(&Impl::on_change, this, unwrapped.get(), _1, _2, _3));
    }
  }

  void on_change(ObservableCollection *self, ObservableCollection::Action a,
                 const ObservableCollection::Key &k,
                 ObservableCollection::Value &v) {
    if (std::holds_alternative<ObservableListPtr>(v)) {
      auto unwrapped = std::get<ObservableListPtr>(v);
      state_connections.push_back(
          std::get<ObservableListPtr>(v)->changed.connect(
              std::bind(&Impl::on_change, this, unwrapped.get(), _1, _2, _3)));
    } else if (std::holds_alternative<ObservableDictPtr>(v)) {
      auto unwrapped = std::get<ObservableDictPtr>(v);
      state_connections.push_back(
          std::get<ObservableDictPtr>(v)->changed.connect(
              std::bind(&Impl::on_change, this, unwrapped.get(), _1, _2, _3)));
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (observable_bridge_clients.empty()) {
      return;
    }

    // Determin if this object is connected to root state.  If it isn't then the
    // state change shouldn't be broadcast
    auto local_root = self;
    while (local_root->parent != nullptr) {
      local_root = local_root->parent;
    }
    if (local_root != this->root) {
      return;
    }

    std::string address = self->address();
    if (std::holds_alternative<std::string>(k)) {
      address += "['" + std::get<std::string>(k) + "']";
    } else if (std::holds_alternative<long long>(k)) {
      address = "[" + std::get<std::string>(k) + "]";
    }
    thalamus_grpc::ObservableTransaction transaction;
    auto change = transaction.add_changes();
    if (a == ObservableCollection::Action::Set) {
      change->set_action(thalamus_grpc::ObservableChange_Action_Set);
    } else {
      change->set_action(thalamus_grpc::ObservableChange_Action_Delete);
    }
    change->set_address(address);
    change->set_value(ObservableCollection::to_string(v));
    for (auto stream : observable_bridge_clients) {
      stream->Write(transaction);
    }
  }

  class ContextGuard {
  public:
    Service *service;
    ::grpc::ServerContext *context;
    ContextGuard(Service *_service, ::grpc::ServerContext *_context)
        : service(_service), context(_context) {
      std::lock_guard<std::mutex> lock(service->impl->mutex);
      service->impl->contexts.insert(context);
    }
    ~ContextGuard() {
      std::lock_guard<std::mutex> lock(service->impl->mutex);
      service->impl->contexts.erase(context);
    }
  };

  ::grpc::Status analog(::grpc::ServerContext *context,
                        const ::thalamus_grpc::AnalogRequest *request,
                        std::function<bool(::thalamus_grpc::AnalogResponse &msg,
                                           ::grpc::WriteOptions options)>
                            writer) {

    Impl::ContextGuard guard(outer, context);
    while (!context->IsCancelled()) {
      std::promise<void> promise;
      auto future = promise.get_future();
      std::shared_ptr<Node> raw_node;
      {
        TRACE_EVENT("thalamus", "Service::analog(get node)");
        boost::asio::post(io_context, [&] {
          node_graph.get_node(request->node(), [&](auto ptr) {
            raw_node = ptr.lock();
            promise.set_value();
          });
        });
        while (future.wait_for(1s) == std::future_status::timeout &&
               !context->IsCancelled()) {
          if (io_context.stopped()) {
            ::thalamus_grpc::AnalogResponse response;
            ::grpc::WriteOptions options;
            options.set_last_message();
            writer(response, options);
            return ::grpc::Status::OK;
          }
        }
        if (!node_cast<AnalogNode *>(raw_node.get())) {
          std::this_thread::sleep_for(1s);
          continue;
        }
      }

      AnalogNode *node = node_cast<AnalogNode *>(raw_node.get());
      std::vector<size_t> channels(request->channels().begin(),
                                   request->channels().end());
      thalamus::vector<std::string> channel_names(
          request->channel_names().begin(), request->channel_names().end());
      auto has_channels = !channels.empty() || !channel_names.empty();

      std::mutex connection_mutex;

      bool channels_changed = true;
      using channels_changed_signal_type = decltype(node->channels_changed);
      boost::signals2::scoped_connection channels_connection =
          node->channels_changed.connect(
              channels_changed_signal_type::slot_type([&](const AnalogNode *) {
                std::unique_lock<std::mutex> lock(connection_mutex);
                channels_changed = true;
              }));

      using signal_type = decltype(raw_node->ready);
      auto connection =
          raw_node->ready.connect(signal_type::slot_type([&](const Node *) {
            if (!node->has_analog_data()) {
              return;
            }
            
            TRACE_EVENT("thalamus", "Service::analog(on ready)");
            std::lock_guard<std::mutex> lock(connection_mutex);
            ::thalamus_grpc::AnalogResponse response;

            size_t num_channels = size_t(node->num_channels());
            if (!has_channels && channels.size() != num_channels) {
              for (auto i = channels.size(); i < num_channels; ++i) {
                channels.push_back(i);
              }
              channels.resize(num_channels);
            }

            if (!channel_names.empty()) {
              std::vector<int> named_channels;
              for (auto &name : channel_names) {
                for (auto i = 0; i < int(num_channels); ++i) {
                  if (node->name(i) == name) {
                    named_channels.push_back(i);
                    break;
                  }
                }
              }
              if (named_channels.size() == channel_names.size()) {
                channels.insert(channels.end(), named_channels.begin(),
                                named_channels.end());
                channel_names.clear();
              } else {
                return;
              }
            }

            response.set_channels_changed(channels_changed);
            response.set_time(size_t(node->time().count()));
            response.set_remote_time(size_t(node->remote_time().count()));
            channels_changed = false;
            for (auto c = 0u; c < channels.size(); ++c) {
              auto channel = channels[c];
              if (channel >= num_channels) {
                continue;
              }
              auto span = response.add_spans();
              span->set_begin(uint32_t(response.data_size()));
              auto name = node->name(int(channel));
              span->set_name(name.data(), name.size());

              response.add_sample_intervals(
                  uint64_t(node->sample_interval(int(channel)).count()));

              visit_node(node, [&](auto wrapper) {
                auto data = wrapper->data(int(channel));
                response.mutable_data()->Add(data.begin(), data.end());
              });

              span->set_end(uint32_t(response.data_size()));
            }
            writer(response, ::grpc::WriteOptions());
          }));
      raw_node.reset();

      while (!context->IsCancelled() && connection.connected()) {
        if (io_context.stopped()) {
          ::grpc::WriteOptions options;
          options.set_last_message();
          ::thalamus_grpc::AnalogResponse response;
          writer(response, options);
          return ::grpc::Status::OK;
        }
        std::this_thread::sleep_for(1s);
      }
      channels_connection.disconnect();
      connection.disconnect();
      std::lock_guard<std::mutex> lock(connection_mutex);
      std::this_thread::sleep_for(1s);
    }

    return ::grpc::Status::OK;
  }
};

Service::Service(ObservableCollection::Value state,
                 boost::asio::io_context &io_context, NodeGraph &node_graph,
                 std::string observable_bridge_redirect)
    : impl(new Impl(state, io_context, node_graph, observable_bridge_redirect,
                    this)) {}
Service::~Service() {}

::grpc::Status
Service::get_modalities(::grpc::ServerContext *context,
                        const ::thalamus_grpc::NodeSelector *request,
                        ::thalamus_grpc::ModalitiesMessage *response) {
  std::shared_ptr<Node> raw_node;
  std::promise<void> promise;
  auto future = promise.get_future();
  boost::asio::post(impl->io_context, [&] {
    impl->node_graph.get_node(*request, [&](auto ptr) {
      raw_node = ptr.lock();
      promise.set_value();
    });
  });
  while (future.wait_for(1s) == std::future_status::timeout &&
         !context->IsCancelled()) {
    if (impl->io_context.stopped()) {
      return ::grpc::Status::OK;
    }
  }
  if (node_cast<AnalogNode *>(raw_node.get())) {
    response->add_values(thalamus_grpc::Modalities::AnalogModality);
  }
  if (node_cast<MotionCaptureNode *>(raw_node.get())) {
    response->add_values(thalamus_grpc::Modalities::MocapModality);
  }
  if (node_cast<ImageNode *>(raw_node.get())) {
    response->add_values(thalamus_grpc::Modalities::ImageModality);
  }
  if (node_cast<TextNode *>(raw_node.get())) {
    response->add_values(thalamus_grpc::Modalities::TextModality);
  }
  return ::grpc::Status::OK;
}

::grpc::Status
Service::get_type_name(::grpc::ServerContext *,
                       const ::thalamus_grpc::StringMessage *request,
                       ::thalamus_grpc::StringMessage *response) {
  auto result = impl->node_graph.get_type_name(request->value());
  response->set_value(result.value_or(""));
  return ::grpc::Status::OK;
}

::grpc::Status Service::get_recommended_channels(
    ::grpc::ServerContext *context,
    const ::thalamus_grpc::NodeSelector *request,
    ::thalamus_grpc::StringListMessage *response) {
  Impl::ContextGuard guard(this, context);

  std::promise<void> promise;
  auto future = promise.get_future();
  std::shared_ptr<Node> raw_node;
  boost::asio::post(impl->io_context, [&] {
    impl->node_graph.get_node(*request, [&](auto ptr) {
      raw_node = ptr.lock();
      AnalogNode *node = node_cast<AnalogNode *>(raw_node.get());
      if (!node) {
        promise.set_value();
        return;
      }
      auto values = node->get_recommended_channels();
      for (const auto &value : values) {
        response->add_value(value);
      }
      promise.set_value();
    });
  });
  while (future.wait_for(1s) == std::future_status::timeout &&
         !context->IsCancelled()) {
    if (impl->io_context.stopped()) {
      return ::grpc::Status::OK;
    }
  }
  return ::grpc::Status::OK;
}

::grpc::Status
Service::node_request(::grpc::ServerContext *,
                      const ::thalamus_grpc::NodeRequest *request,
                      ::thalamus_grpc::NodeResponse *response) {
  auto weak = impl->node_graph.get_node(request->node());
  auto node = weak.lock();
  if (!node) {
    return ::grpc::Status::OK;
  }

  auto parsed = boost::json::parse(request->json());
  auto json_response = node->process(parsed);
  auto serialized_response = boost::json::serialize(json_response);
  response->set_json(serialized_response);

  return ::grpc::Status::OK;
}

::grpc::Status Service::node_request_stream(
    ::grpc::ServerContext *,
    ::grpc::ServerReaderWriter<::thalamus_grpc::NodeResponse,
                               ::thalamus_grpc::NodeRequest> *stream) {

  std::weak_ptr<Node> weak;
  ::thalamus_grpc::NodeRequest request;
  ::thalamus_grpc::NodeResponse response;
  while (stream->Read(&request)) {
    if (!request.node().empty()) {
      weak = impl->node_graph.get_node(request.node());
    }
    auto node = weak.lock();
    response.set_id(request.id());
    if (!node) {
      response.clear_json();
      response.set_status(
          thalamus_grpc::NodeResponse::Status::NodeResponse_Status_NOT_FOUND);
      continue;
    }

    auto parsed = boost::json::parse(request.json());
    auto json_response = node->process(parsed);
    auto serialized_response = boost::json::serialize(json_response);
    response.set_json(serialized_response);
    response.set_status(
        thalamus_grpc::NodeResponse::Status::NodeResponse_Status_OK);
    stream->Write(response);
  }
  return ::grpc::Status::OK;
}

::grpc::Status
Service::events(::grpc::ServerContext *context,
                ::grpc::ServerReader<::thalamus_grpc::Event> *reader,
                ::thalamus_grpc::Empty *) {
  set_current_thread_name("events");
  Impl::ContextGuard guard(this, context);
  ::thalamus_grpc::Event the_event;
  while (reader->Read(&the_event)) {
    TRACE_EVENT("thalamus", "events");
    std::promise<void> promise;
    auto future = promise.get_future();
    boost::asio::post(impl->io_context, [&] {
      TRACE_EVENT("thalamus", "events(post)");
      events_signal(the_event);
      promise.set_value();
    });
    while (future.wait_for(1s) == std::future_status::timeout &&
           !context->IsCancelled()) {
    }
  }
  return ::grpc::Status::OK;
}

::grpc::Status Service::log(::grpc::ServerContext *context,
                            ::grpc::ServerReader<::thalamus_grpc::Text> *reader,
                            ::thalamus_grpc::Empty *) {
  set_current_thread_name("log");
  Impl::ContextGuard guard(this, context);
  ::thalamus_grpc::Text the_text;
  while (reader->Read(&the_text)) {
    TRACE_EVENT("thalamus", "log");
    std::promise<void> promise;
    auto future = promise.get_future();
    boost::asio::post(impl->io_context, [&] {
      TRACE_EVENT("thalamus", "log(post)");
      log_signal(the_text);
      promise.set_value();
    });
    while (future.wait_for(1s) == std::future_status::timeout &&
           !context->IsCancelled()) {
    }
  }
  return ::grpc::Status::OK;
}

::grpc::Status Service::observable_bridge(
    ::grpc::ServerContext *,
    ::grpc::ServerReaderWriter<::thalamus_grpc::ObservableChange,
                               ::thalamus_grpc::ObservableChange> *) {
  return ::grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Unimplemented");
}

::grpc::Status Service::observable_bridge_v2(
    ::grpc::ServerContext *context,
    ::grpc::ServerReaderWriter<::thalamus_grpc::ObservableTransaction,
                               ::thalamus_grpc::ObservableTransaction>
        *stream) {
  Impl::ContextGuard guard(this, context);
  thalamus_grpc::ObservableTransaction in;
  thalamus_grpc::ObservableTransaction out;
  bool thread_name_set = false;

  if (!impl->observable_bridge_redirect.empty()) {
    out.set_redirection(impl->observable_bridge_redirect);
    stream->WriteLast(out, ::grpc::WriteOptions());
    return ::grpc::Status::OK;
  }

  {
    std::unique_lock<std::mutex> lock(impl->mutex);
    grpc::internal::WriterInterface<thalamus_grpc::ObservableTransaction>
        *writer = stream;
    impl->observable_bridge_clients.push_back(writer);
  }

  while (stream->Read(&in)) {
    TRACE_EVENT("thalamus", "observable_bridge");
    if (!thread_name_set && !in.peer_name().empty()) {
      set_current_thread_name(
          absl::StrFormat("observable_bridge %s", in.peer_name()));
      thread_name_set = true;
    }

    std::vector<std::promise<void>> promises;
    std::vector<std::future<void>> futures;
    for (auto &change : in.changes()) {
      boost::json::value parsed = boost::json::parse(change.value());
      auto value = ObservableCollection::from_json(parsed);

      promises.emplace_back();
      futures.push_back(promises.back().get_future());
      boost::asio::post(impl->io_context,
                        [&promise = promises.back(), state = impl->state,
                         action = change.action(), address = change.address(),
                         moved_value = std::move(value)] {
                          TRACE_EVENT("thalamus", "observable_bridge(post)");
                          // change_signal(change);
                          if (action ==
                              thalamus_grpc::ObservableChange_Action_Set) {
                            set_jsonpath(state, address, moved_value, true);
                          } else {
                            delete_jsonpath(state, address, true);
                          }
                          promise.set_value();
                        });
    }
    for (auto &future : futures) {
      while (future.wait_for(1s) == std::future_status::timeout &&
             !context->IsCancelled() && !impl->io_context.stopped()) {
      }
      if (context->IsCancelled() || impl->io_context.stopped()) {
        return ::grpc::Status::OK;
      }
    }
    out.set_acknowledged(in.id());
    stream->Write(out);
  }

  {
    std::unique_lock<std::mutex> lock(impl->mutex);
    auto i = std::find(impl->observable_bridge_clients.begin(),
                       impl->observable_bridge_clients.end(), stream);
    impl->observable_bridge_clients.erase(i);
  }
  return ::grpc::Status::OK;
}

::grpc::Status Service::observable_bridge_read(
    ::grpc::ServerContext *context,
    const ::thalamus_grpc::ObservableReadRequest *request,
    ::grpc::ServerWriter<::thalamus_grpc::ObservableTransaction> *stream) {
  Impl::ContextGuard guard(this, context);
  thalamus_grpc::ObservableTransaction in;
  thalamus_grpc::ObservableTransaction out;

  if (!impl->observable_bridge_redirect.empty()) {
    out.set_redirection(impl->observable_bridge_redirect);
    stream->WriteLast(out, ::grpc::WriteOptions());
    return ::grpc::Status::OK;
  }

  {
    std::unique_lock<std::mutex> lock(impl->mutex);
    impl->observable_bridge_clients.push_back(stream);
    impl->peer_name_to_observable_bridge_client[request->peer_name()] = stream;
  }
  impl->condition.notify_all();

  while (!context->IsCancelled() && !impl->io_context.stopped()) {
    std::this_thread::sleep_for(1s);
  }

  {
    std::unique_lock<std::mutex> lock(impl->mutex);
    auto i = std::find(impl->observable_bridge_clients.begin(),
                       impl->observable_bridge_clients.end(), stream);
    impl->observable_bridge_clients.erase(i);
  }
  return ::grpc::Status::OK;
}

::grpc::Status Service::observable_bridge_write(
    ::grpc::ServerContext *context,
    const ::thalamus_grpc::ObservableTransaction *request,
    ::thalamus_grpc::Empty *) {
  std::vector<std::promise<void>> promises;
  std::vector<std::future<void>> futures;
  for (auto &change : request->changes()) {
    boost::json::value parsed = boost::json::parse(change.value());
    auto value = ObservableCollection::from_json(parsed);

    promises.emplace_back();
    futures.push_back(promises.back().get_future());
    boost::asio::post(
        impl->io_context, [&promise = promises.back(), state = impl->state,
                           action = change.action(), address = change.address(),
                           moved_value = std::move(value)] {
          TRACE_EVENT("thalamus", "observable_bridge(post)");
          // change_signal(change);
          if (action == thalamus_grpc::ObservableChange_Action_Set) {
            set_jsonpath(state, address, moved_value, true);
          } else {
            delete_jsonpath(state, address, true);
          }
          promise.set_value();
        });
  }
  for (auto &future : futures) {
    while (future.wait_for(1s) == std::future_status::timeout &&
           !context->IsCancelled() && !impl->io_context.stopped()) {
    }
    if (context->IsCancelled() || impl->io_context.stopped()) {
      return ::grpc::Status::OK;
    }
  }

  {
    std::unique_lock<std::mutex> lock(impl->mutex);
    impl->condition.wait_for(lock, 5s, [&] {
      return impl->peer_name_to_observable_bridge_client.contains(
          request->peer_name());
    });
    auto i = impl->peer_name_to_observable_bridge_client[request->peer_name()];
    thalamus_grpc::ObservableTransaction acknowledgement;
    acknowledgement.set_acknowledged(request->id());
    i->Write(acknowledgement);
  }
  return ::grpc::Status::OK;
}

::grpc::Status Service::inject_analog(
    ::grpc::ServerContext *context,
    ::grpc::ServerReader<::thalamus_grpc::InjectAnalogRequest> *reader,
    ::thalamus_grpc::Empty *) {
  set_current_thread_name("inject_analog");
  Impl::ContextGuard guard(this, context);
  ::thalamus_grpc::InjectAnalogRequest request;
  std::string node_name;
  if (!reader->Read(&request)) {
    THALAMUS_LOG(error) << "Couldn't read name of node to inject into";
    return ::grpc::Status::OK;
  }
  if (!request.has_node()) {
    THALAMUS_LOG(error)
        << "First message of inject_analog request should contain node name";
    return ::grpc::Status::OK;
  }

  node_name = request.node();

  while (!context->IsCancelled()) {
    std::promise<void> promise;
    auto future = promise.get_future();
    std::shared_ptr<Node> raw_node;
    boost::asio::post(impl->io_context, [&] {
      impl->node_graph.get_node(node_name, [&](auto ptr) {
        raw_node = ptr.lock();
        promise.set_value();
      });
    });
    THALAMUS_LOG(info) << "Waiting for node";
    while (future.wait_for(1s) == std::future_status::timeout &&
           !context->IsCancelled()) {
      if (impl->io_context.stopped()) {
        return ::grpc::Status::OK;
      }
    }
    if (context->IsCancelled()) {
      continue;
    }
    THALAMUS_LOG(info) << "Got node";

    AnalogNode *node = node_cast<AnalogNode *>(raw_node.get());
    if (!node) {
      std::this_thread::sleep_for(1s);
      continue;
    }

    thalamus::vector<std::span<const double>> spans;
    thalamus::vector<std::chrono::nanoseconds> sample_intervals;
    thalamus::vector<std::string_view> names;
    bool first = true;
    while (reader->Read(&request)) {
      if (request.has_node()) {
        node_name = request.node();
        break;
      }

      auto &data = request.signal().data();
      spans.clear();
      names.clear();
      for (auto &span : request.signal().spans()) {
        spans.emplace_back(data.begin() + span.begin(),
                           data.begin() + span.end());
        names.emplace_back(span.name().begin(), span.name().end());
      }
      sample_intervals.clear();
      for (auto &interval : request.signal().sample_intervals()) {
        sample_intervals.emplace_back(interval);
      }

      std::promise<void> inject_promise;
      auto inject_future = inject_promise.get_future();
      boost::asio::post(impl->io_context, [&] {
        if (first || request.signal().channels_changed()) {
          node->channels_changed(node);
          first = false;
        }
        node->inject(spans, sample_intervals, names);
        inject_promise.set_value();
      });
      while (inject_future.wait_for(1s) == std::future_status::timeout &&
             !context->IsCancelled()) {
      }
    }
  }
  return ::grpc::Status::OK;
}

struct Counter {
  static std::atomic_size_t count;
  std::string label;
  Counter(const std::string &_label) : label(_label) {
    THALAMUS_LOG(info) << label << " add " << ++count;
  }
  ~Counter() { THALAMUS_LOG(info) << label << " remove " << --count; }
};
std::atomic_size_t Counter::count = 0;

::grpc::Status
Service::graph(::grpc::ServerContext *context,
               const ::thalamus_grpc::GraphRequest *request,
               ::grpc::ServerWriter<::thalamus_grpc::GraphResponse> *writer) {
  Impl::ContextGuard guard(this, context);
  std::stringstream stream;
  stream << request->node().name();
  for (auto &name : request->channel_names()) {
    stream << " " << name;
  }
  Counter counter(stream.str());
  while (!context->IsCancelled()) {
    std::promise<void> promise;
    auto future = promise.get_future();
    std::shared_ptr<Node> raw_node;
    boost::asio::post(impl->io_context, [&] {
      impl->node_graph.get_node(request->node(), [&](auto ptr) {
        raw_node = ptr.lock();
        promise.set_value();
      });
    });
    while (future.wait_for(1s) == std::future_status::timeout &&
           !context->IsCancelled()) {
      if (impl->io_context.stopped()) {
        writer->WriteLast(::thalamus_grpc::GraphResponse(),
                          ::grpc::WriteOptions());
        return ::grpc::Status::OK;
      }
    }
    if (!node_cast<AnalogNode *>(raw_node.get())) {
      std::this_thread::sleep_for(1s);
      continue;
    }

    AnalogNode *node = node_cast<AnalogNode *>(raw_node.get());
    std::vector<size_t> channels(request->channels().begin(),
                                 request->channels().end());

    std::chrono::nanoseconds bin_ns(request->bin_ns());
    std::mutex connection_mutex;
    std::vector<double> mins(channels.size(),
                             std::numeric_limits<double>::max());
    std::vector<double> maxs(channels.size(),
                             -std::numeric_limits<double>::max());
    std::vector<double> previous_mins(channels.size(), 0);
    std::vector<double> previous_maxes(channels.size(), 0);
    std::vector<std::chrono::nanoseconds> current_times(channels.size());
    std::vector<std::chrono::nanoseconds> bin_ends(channels.size(), bin_ns);
    thalamus::vector<std::string> channel_names(
        request->channel_names().begin(), request->channel_names().end());
    std::optional<std::chrono::nanoseconds> first_time;
    bool channels_changed = true;

    auto has_channels = !channels.empty() || !channel_names.empty();

    using channels_changed_signal_type = decltype(node->channels_changed);
    boost::signals2::scoped_connection channels_connection =
        node->channels_changed.connect(
            channels_changed_signal_type::slot_type([&](const AnalogNode *) {
              std::unique_lock<std::mutex> lock(connection_mutex);
              channels_changed = true;
            }));

    using signal_type = decltype(raw_node->ready);
    auto connection =
        raw_node->ready.connect(signal_type::slot_type([&](const Node *) {
          if (!node->has_analog_data()) {
            return;
          }
          std::lock_guard<std::mutex> lock(connection_mutex);
          ::thalamus_grpc::GraphResponse response;
          auto num_channels = size_t(node->num_channels());
          if (!has_channels && channels.size() != num_channels) {
            for (auto i = channels.size(); i < num_channels; ++i) {
              channels.push_back(i);
            }
            channels.resize(num_channels);
            mins.resize(num_channels, std::numeric_limits<double>::max());
            maxs.resize(num_channels, -std::numeric_limits<double>::max());
            previous_mins.resize(num_channels);
            previous_maxes.resize(num_channels);
            current_times.resize(num_channels);
            bin_ends.resize(num_channels, bin_ns);
          }

          if (!channel_names.empty()) {
            std::vector<int> named_channels;
            for (auto &name : channel_names) {
              for (auto i = 0; i < int(num_channels); ++i) {
                if (node->name(i) == name) {
                  named_channels.push_back(i);
                  break;
                }
              }
            }
            if (named_channels.size() == channel_names.size()) {
              channels.insert(channels.end(), named_channels.begin(),
                              named_channels.end());
              channel_names.clear();

              channels.resize(channels.size());
              mins.resize(channels.size(), std::numeric_limits<double>::max());
              maxs.resize(channels.size(), -std::numeric_limits<double>::max());
              previous_mins.resize(channels.size());
              previous_maxes.resize(channels.size());
              current_times.resize(channels.size());
              bin_ends.resize(channels.size(), bin_ns);
            } else {
              return;
            }
          }

          response.set_channels_changed(channels_changed);
          channels_changed = false;
          if (!first_time) {
            first_time = node->time();
          }
          for (auto c = 0u; c < channels.size(); ++c) {
            auto channel = channels[c];
            auto &min = mins[c];
            auto &max = maxs[c];
            auto &current_time = current_times[c];
            auto &bin_end = bin_ends[c];
            if (channel >= num_channels) {
              continue;
            }
            auto span = response.add_spans();
            span->set_begin(uint32_t(response.bins_size()));

            auto interval = node->sample_interval(int(channel));
            if (interval == 0ns) {
              current_time = node->time() - *first_time;
            }
            visit_node(node, [&](auto wrapper) {
              auto data = wrapper->data(int(channel));
              for (double sample : data) {
                auto wrote = current_time >= bin_end;
                while (current_time >= bin_end) {
                  response.add_bins(min);
                  response.add_bins(max);
                  bin_end += bin_ns;
                }
                if (wrote) {
                  min = std::numeric_limits<double>::max();
                  max = -std::numeric_limits<double>::max();
                }
                min = std::min(min, sample);
                max = std::max(max, sample);
                current_time += interval;
              }
            });
            span->set_end(uint32_t(response.bins_size()));
            auto name = node->name(int(channel));
            span->set_name(name.data(), name.size());
          }
          writer->Write(response);
        }));
    raw_node.reset();

    while (!context->IsCancelled() && connection.connected()) {
      if (impl->io_context.stopped()) {
        writer->WriteLast(::thalamus_grpc::GraphResponse(),
                          ::grpc::WriteOptions());
        return ::grpc::Status::OK;
      }
      std::this_thread::sleep_for(1s);
    }
    channels_connection.disconnect();
    connection.disconnect();
    std::lock_guard<std::mutex> lock(connection_mutex);
    std::this_thread::sleep_for(1s);
  }

  return ::grpc::Status::OK;
}

::grpc::Status Service::channel_info(
    ::grpc::ServerContext *context,
    const ::thalamus_grpc::AnalogRequest *request,
    ::grpc::ServerWriter<::thalamus_grpc::AnalogResponse> *writer) {
  Impl::ContextGuard guard(this, context);
  while (!context->IsCancelled()) {
    std::promise<void> promise;
    auto future = promise.get_future();
    std::weak_ptr<Node> weak_raw_node;
    boost::asio::post(impl->io_context, [&] {
      impl->node_graph.get_node(request->node(), [&](auto ptr) {
        weak_raw_node = ptr;
        promise.set_value();
      });
    });
    while (future.wait_for(1s) == std::future_status::timeout &&
           !context->IsCancelled()) {
      if (impl->io_context.stopped()) {
        ::thalamus_grpc::AnalogResponse response;
        ::grpc::WriteOptions options;
        options.set_last_message();
        writer->Write(response, options);
        return ::grpc::Status::OK;
      }
    }
    auto raw_node = weak_raw_node.lock();
    if (!node_cast<AnalogNode *>(raw_node.get())) {
      std::this_thread::sleep_for(1s);
      continue;
    }

    AnalogNode *node = node_cast<AnalogNode *>(raw_node.get());
    std::vector<size_t> channels(request->channels().begin(),
                                 request->channels().end());
    thalamus::vector<std::string> channel_names(
        request->channel_names().begin(), request->channel_names().begin());

    std::mutex connection_mutex;
    std::mutex cond_mutex;
    std::condition_variable cond;
    bool channels_changed = true;

    using channels_changed_signal_type = decltype(node->channels_changed);
    using signal_type = decltype(raw_node->ready);

    boost::signals2::scoped_connection channels_connection =
        node->channels_changed.connect(
            channels_changed_signal_type::slot_type([&](const AnalogNode *) {
              if (!connection_mutex.try_lock()) {
                return;
              }
              std::lock_guard<std::mutex> lock(connection_mutex,
                                               std::adopt_lock_t());
              std::unique_lock<std::mutex> lock2(cond_mutex);
              channels_changed = true;
              cond.notify_one();
            }));
    raw_node.reset();

    while (!context->IsCancelled() && channels_connection.connected()) {
      if (impl->io_context.stopped()) {
        return ::grpc::Status::OK;
      }
      {
        std::unique_lock<std::mutex> lock(cond_mutex);
        cond.wait_for(lock, 1s);
        if (!channels_changed) {
          continue;
        }
        channels_changed = false;
      }

      std::mutex info_mutex;
      std::condition_variable info_cond;
      bool got_info = false;
      raw_node = weak_raw_node.lock();
      if (!raw_node) {
        break;
      }

      boost::signals2::scoped_connection connection =
          raw_node->ready.connect(signal_type::slot_type([&](const Node *) {
            if (!connection_mutex.try_lock()) {
              return;
            }
            std::lock_guard<std::mutex> lock(connection_mutex,
                                             std::adopt_lock_t());
            ::thalamus_grpc::AnalogResponse response;

            for (auto c = 0; c < node->num_channels(); ++c) {
              auto span = response.add_spans();
              auto name = node->name(c);
              span->set_name(name.data(), name.size());
              response.add_sample_intervals(
                  uint64_t(node->sample_interval(c).count()));
            }
            writer->Write(response, ::grpc::WriteOptions());

            {
              std::lock_guard<std::mutex> lock2(info_mutex);
              got_info = true;
            }
            info_cond.notify_one();
            connection.disconnect();
          }));
      raw_node.reset();

      {
        std::unique_lock<std::mutex> lock2(info_mutex);
        auto predicate = [&] {
          return context->IsCancelled() || !connection.connected() ||
                 impl->io_context.stopped() || got_info;
        };
        while (!predicate()) {
          info_cond.wait_for(lock2, 1s, predicate);
        }
      }
      connection.disconnect();
      std::lock_guard<std::mutex> lock2(connection_mutex);
    }

    channels_connection.disconnect();
    std::lock_guard<std::mutex> lock(connection_mutex);
    std::this_thread::sleep_for(1s);
  }

  return ::grpc::Status::OK;
}

::grpc::Status Service::spectrogram(
    ::grpc::ServerContext *context,
    const ::thalamus_grpc::SpectrogramRequest *request,
    ::grpc::ServerWriter<::thalamus_grpc::SpectrogramResponse> *writer) {
  Impl::ContextGuard guard(this, context);
  while (!context->IsCancelled()) {
    std::promise<void> promise;
    auto future = promise.get_future();
    std::shared_ptr<Node> raw_node;
    boost::asio::post(impl->io_context, [&] {
      impl->node_graph.get_node(request->node(), [&](auto ptr) {
        raw_node = ptr.lock();
        promise.set_value();
      });
    });
    while (future.wait_for(1s) == std::future_status::timeout &&
           !context->IsCancelled()) {
      if (impl->io_context.stopped()) {
        writer->WriteLast(::thalamus_grpc::SpectrogramResponse(),
                          ::grpc::WriteOptions());
        return ::grpc::Status::OK;
      }
    }
    if (!node_cast<AnalogNode *>(raw_node.get())) {
      std::this_thread::sleep_for(1s);
      continue;
    }

    AnalogNode *node = node_cast<AnalogNode *>(raw_node.get());
    std::vector<std::vector<double>> accumulated_data(
        size_t(request->channels().size()));
    std::chrono::nanoseconds window_ns(
        static_cast<size_t>(request->window_s() * 1e9));
    std::vector<int> window_samples(size_t(request->channels().size()));
    std::chrono::nanoseconds hop_ns(
        static_cast<size_t>(request->hop_s() * 1e9));
    thalamus::map<size_t, std::vector<double>> windows;
    thalamus::map<int, size_t> window_sizes;

    std::mutex connection_mutex;
    std::vector<std::chrono::nanoseconds> countdowns;

    std::set<int> channel_ids_set;
    std::vector<int> channel_ids;
    std::set<std::string> unlocated_channels;
    for (auto &c : request->channels()) {
      if (!c.name().empty()) {
        unlocated_channels.insert(c.name());
      } else {
        channel_ids_set.insert(c.index());
      }
      channel_ids.assign(channel_ids_set.begin(), channel_ids_set.end());
    }

    std::vector<thalamus_grpc::ChannelId> output_channel_ids;

    using signal_type = decltype(raw_node->ready);
    auto connection =
        raw_node->ready.connect(signal_type::slot_type([&](const Node *) {
          if (!node->has_analog_data()) {
            return;
          }
          if (!connection_mutex.try_lock()) {
            return;
          }
          TRACE_EVENT("thalamus", "Service::spectrogram");
          std::lock_guard<std::mutex> lock(connection_mutex,
                                           std::adopt_lock_t());
          int num_channels = node->num_channels();

          if (!unlocated_channels.empty()) {
            for (auto i = 0; i < num_channels; ++i) {
              auto name_view = node->name(i);
              std::string name(name_view.begin(), name_view.end());
              if (unlocated_channels.contains(name)) {
                channel_ids_set.insert(i);
                unlocated_channels.erase(name);
              }
            }
            channel_ids.assign(channel_ids_set.begin(), channel_ids_set.end());
          }

          if (!unlocated_channels.empty()) {
            return;
          }

          countdowns.resize(size_t(num_channels));
          accumulated_data.resize(size_t(num_channels));

          for (auto c = 0u; c < channel_ids.size(); ++c) {
            auto channel = channel_ids[c];
            visit_node(node, [&](auto wrapper) {
              auto data = wrapper->data(channel);
              auto interval = wrapper->sample_interval(channel);
              if (interval.count() == 0) {
                return;
              }
              auto &countdown = countdowns[size_t(channel)];
              size_t skips = size_t(countdown / interval);
              if (skips > data.size()) {
                skips = data.size();
              }

              auto &accumulated_channel = accumulated_data.at(size_t(channel));
              accumulated_channel.insert(accumulated_channel.end(),
                                         data.begin() + int64_t(skips),
                                         data.end());
              countdown -= skips * interval;
            });
          }

          auto working = true;
          while (working) {
            working = false;
            ::thalamus_grpc::SpectrogramResponse response;

            for (auto c = 0u; c < channel_ids.size(); ++c) {
              auto channel = channel_ids[c];
              auto interval = node->sample_interval(channel);
              if (interval.count() == 0) {
                continue;
              }
              auto name = node->name(channel);
              auto &countdown = countdowns[size_t(channel)];
              if (countdown >= interval) {
                continue;
              }

              auto needed_window_samples = window_ns / interval;
              if (!window_sizes.contains(int(needed_window_samples))) {
                auto i = 1;
                while (i < needed_window_samples) {
                  i <<= 1;
                }
                window_sizes[int(needed_window_samples)] = size_t(i);
              }
              auto window_size = window_sizes[int(needed_window_samples)];

              auto &accumulated_channel = accumulated_data.at(size_t(channel));
              if (accumulated_channel.size() >= window_size) {
                auto samples = window_size;
                if (!windows.contains(samples)) {
                  auto &window = windows[samples];
                  window.assign(samples, 0);
                  for (auto n = 0ull; n < window.size(); ++n) {
                    window[n] = .54 * (1 - .54) *
                                std::cos(2 * M_PI * double(n) /
                                         double(window.size() - 1));
                  }
                }
                auto &window = windows.at(samples);
                std::vector<double> accumulated_copy(1);
                accumulated_copy.insert(
                    accumulated_copy.end(), accumulated_channel.begin(),
                    accumulated_channel.begin() + int64_t(samples));

                for (auto j = 0ull; j < window.size(); ++j) {
                  accumulated_copy[1 + j] *= window[j];
                }
                {
                  TRACE_EVENT("thalamus", "Service::realft");
                  realft(accumulated_copy.data(),
                         uint32_t(accumulated_copy.size() - 1), 1);
                }

                auto spectrogram = response.add_spectrograms();
                spectrogram->mutable_channel()->set_index(channel);
                spectrogram->mutable_channel()->set_name(name.data(),
                                                         name.size());
                spectrogram->set_max_frequency(.5e9 / double(interval.count()));

                spectrogram->mutable_data()->Add(accumulated_copy.begin() + 1,
                                                 accumulated_copy.end());

                auto skips = hop_ns / interval;
                if (skips == 0) {
                  skips = 1;
                }
                auto end = std::min(accumulated_channel.begin() + skips,
                                    accumulated_channel.end());
                auto count = std::distance(accumulated_channel.begin(), end);
                accumulated_channel.erase(accumulated_channel.begin(), end);
                countdown = std::max(hop_ns - count * interval, 0ns);
              }
            }
            working = response.spectrograms_size() > 0;
            if (working) {
              writer->Write(response);
            }
          }
        }));
    raw_node.reset();

    while (!context->IsCancelled() && connection.connected()) {
      if (impl->io_context.stopped()) {
        writer->WriteLast(::thalamus_grpc::SpectrogramResponse(),
                          ::grpc::WriteOptions());
        return ::grpc::Status::OK;
      }
      std::this_thread::sleep_for(1s);
    }
    connection.disconnect();
    std::lock_guard<std::mutex> lock(connection_mutex);
    std::this_thread::sleep_for(1s);
  }

  return ::grpc::Status::OK;
}

::grpc::Status
Service::analog(::grpc::ServerContext *context,
                const ::thalamus_grpc::AnalogRequest *request,
                ::grpc::ServerWriter<::thalamus_grpc::AnalogResponse> *writer) {
  return impl->analog(context, request,
                      [&](const ::thalamus_grpc::AnalogResponse &msg,
                          const ::grpc::WriteOptions &options) {
                        return writer->Write(msg, options);
                      });
}

::grpc::Status Service::remote_node(
    ::grpc::ServerContext *context,
    ::grpc::ServerReaderWriter<::thalamus_grpc::RemoteNodeMessage,
                               ::thalamus_grpc::RemoteNodeMessage> *stream) {
  ::thalamus_grpc::RemoteNodeMessage message;
  stream->Read(&message);
  auto request = message.release_request();

  std::thread ping_thread([&] {
    while (stream->Read(&message)) {
      THALAMUS_ASSERT(message.has_ping(), "Expected ping message");
      auto ping = message.ping();

      ::thalamus_grpc::RemoteNodeMessage response;
      auto pong = response.mutable_pong();
      pong->set_id(ping.id());

      stream->Write(response);
    }
  });

  auto result = impl->analog(context, request,
                             [&](::thalamus_grpc::AnalogResponse &msg,
                                 const ::grpc::WriteOptions &options) {
                               ::thalamus_grpc::RemoteNodeMessage response;
                               response.mutable_data()->Swap(&msg);
                               return stream->Write(response, options);
                             });

  ping_thread.join();
  return result;
}

::grpc::Status
Service::xsens(::grpc::ServerContext *context,
               const ::thalamus_grpc::NodeSelector *request,
               ::grpc::ServerWriter<::thalamus_grpc::XsensResponse> *writer) {
  Impl::ContextGuard guard(this, context);
  while (!context->IsCancelled()) {
    std::promise<void> promise;
    auto future = promise.get_future();
    std::shared_ptr<Node> raw_node;
    {
      TRACE_EVENT("thalamus", "Service::xsens(get node)");
      boost::asio::post(impl->io_context, [&] {
        impl->node_graph.get_node(*request, [&](auto ptr) {
          raw_node = ptr.lock();
          promise.set_value();
        });
      });
      while (future.wait_for(1s) == std::future_status::timeout &&
             !context->IsCancelled()) {
        if (impl->io_context.stopped()) {
          writer->WriteLast(::thalamus_grpc::XsensResponse(),
                            ::grpc::WriteOptions());
          return ::grpc::Status::OK;
        }
      }
      if (!node_cast<MotionCaptureNode *>(raw_node.get())) {
        std::this_thread::sleep_for(1s);
        continue;
      }
    }

    auto node = node_cast<MotionCaptureNode *>(raw_node.get());

    std::mutex connection_mutex;

    using signal_type = decltype(raw_node->ready);
    auto connection = raw_node->ready.connect(
        signal_type::slot_type([&](const Node *) {
          TRACE_EVENT("thalamus", "Service::xsens(on ready)");
          std::lock_guard<std::mutex> lock(connection_mutex);
          ::thalamus_grpc::XsensResponse response;
          response.set_pose_name(node->pose_name());
          auto data = node->segments();
          for (auto &segment : data) {
            auto response_segment = response.add_segments();
            response_segment->set_id(segment.segment_id);
            response_segment->set_x(boost::qvm::X(segment.position));
            response_segment->set_y(boost::qvm::Y(segment.position));
            response_segment->set_z(boost::qvm::Z(segment.position));
            response_segment->set_q0(boost::qvm::S(segment.rotation));
            response_segment->set_q1(boost::qvm::X(segment.rotation));
            response_segment->set_q2(boost::qvm::Y(segment.rotation));
            response_segment->set_q3(boost::qvm::Z(segment.rotation));
          }
          writer->Write(response);
        }).track_foreign(raw_node));
    raw_node.reset();

    while (!context->IsCancelled() && connection.connected()) {
      if (impl->io_context.stopped()) {
        writer->WriteLast(::thalamus_grpc::XsensResponse(),
                          ::grpc::WriteOptions());
        return ::grpc::Status::OK;
      }
      std::this_thread::sleep_for(1s);
    }
    connection.disconnect();
    std::lock_guard<std::mutex> lock(connection_mutex);
    std::this_thread::sleep_for(1s);
  }

  return ::grpc::Status::OK;
}

static const size_t IMAGE_CHUNK_SIZE = 524288;

::grpc::Status
Service::image(::grpc::ServerContext *context,
               const ::thalamus_grpc::ImageRequest *request,
               ::grpc::ServerWriter<::thalamus_grpc::Image> *writer) {
  Impl::ContextGuard guard(this, context);
  while (!context->IsCancelled()) {
    std::promise<void> promise;
    auto future = promise.get_future();
    std::shared_ptr<Node> raw_node;
    {
      TRACE_EVENT("thalamus", "Service::image(get node)");
      boost::asio::post(impl->io_context, [&] {
        impl->node_graph.get_node(request->node(), [&](auto ptr) {
          raw_node = ptr.lock();
          promise.set_value();
        });
      });
      while (future.wait_for(1s) == std::future_status::timeout &&
             !context->IsCancelled()) {
        if (impl->io_context.stopped()) {
          writer->WriteLast(::thalamus_grpc::Image(), ::grpc::WriteOptions());
          return ::grpc::Status::OK;
        }
      }
      if (!node_cast<ImageNode *>(raw_node.get())) {
        std::this_thread::sleep_for(1s);
        continue;
      }
    }

    auto node = node_cast<ImageNode *>(raw_node.get());

    std::mutex connection_mutex;
    std::mutex images_mutex;
    std::condition_variable cond;
    std::vector<thalamus_grpc::Image> images;
    std::vector<std::chrono::steady_clock::time_point> frame_times;

    using signal_type = decltype(raw_node->ready);
    auto connection = raw_node->ready.connect(
        signal_type::slot_type([&](const Node *) {
          TRACE_EVENT("thalamus", "Service::image(on ready)");
          std::lock_guard<std::mutex> lock(connection_mutex);
          std::vector<::thalamus_grpc::Image> responses;
          size_t position = 0;

          if (request->framerate() > 0) {
            auto now = std::chrono::steady_clock::now();
            while (!frame_times.empty() && now - frame_times.front() >= 1s) {
              std::pop_heap(frame_times.begin(), frame_times.end(),
                            [](auto &l, auto &r) { return l > r; });
              frame_times.pop_back();
            }
            if (!frame_times.empty()) {
              auto duration = now - frame_times.front();
              auto duration_seconds =
                  double(duration.count()) / decltype(duration)::period::den;
              if (double(frame_times.size()) / duration_seconds >=
                  request->framerate()) {
                return;
              }
            }

            frame_times.push_back(now);
            std::push_heap(frame_times.begin(), frame_times.end(),
                           [](auto &l, auto &r) { return l > r; });
          }

          size_t data_count = 0;
          for (auto i = 0ull; i < node->num_planes(); ++i) {
            auto data = node->plane(int(i));
            data_count += data.size();
          }

          auto width = node->width();
          auto height = node->height();
          thalamus_grpc::Image::Format format;
          switch (node->format()) {
          case ImageNode::Format::Gray:
            format = thalamus_grpc::Image::Format::Image_Format_Gray;
            break;
          case ImageNode::Format::RGB:
            format = thalamus_grpc::Image::Format::Image_Format_RGB;
            break;
          case ImageNode::Format::YUYV422:
            format = thalamus_grpc::Image::Format::Image_Format_YUYV422;
            break;
          case ImageNode::Format::YUV420P:
            format = thalamus_grpc::Image::Format::Image_Format_YUV420P;
            break;
          case ImageNode::Format::YUVJ420P:
            format = thalamus_grpc::Image::Format::Image_Format_YUVJ420P;
            break;
          }

          while (position < data_count) {
            responses.emplace_back();
            auto &piece = responses.back();
            piece.set_width(uint32_t(width));
            piece.set_height(uint32_t(height));
            piece.set_format(format);

            size_t plane_offset = 0;
            size_t remaining_chunk = IMAGE_CHUNK_SIZE;
            for (auto i = 0ull; i < node->num_planes(); ++i) {
              auto data = node->plane(int(i));
              if (position > plane_offset + data.size() || !remaining_chunk) {
                piece.add_data();
              } else {
                auto in_plane_offset = position - plane_offset;
                auto count =
                    std::min(data.size() - in_plane_offset, remaining_chunk);
                piece.add_data(data.data() + in_plane_offset, count);
                remaining_chunk -= count;
                position += count;
              }
              plane_offset += data.size();
            }
          }
          responses.back().set_last(true);

          std::lock_guard<std::mutex> lock2(images_mutex);
          for (auto &i : responses) {
            images.push_back(std::move(i));
          }
          cond.notify_one();
        }).track_foreign(raw_node));
    raw_node.reset();

    while (!context->IsCancelled() && connection.connected()) {
      if (impl->io_context.stopped()) {
        writer->WriteLast(::thalamus_grpc::Image(), ::grpc::WriteOptions());
        return ::grpc::Status::OK;
      }
      std::vector<thalamus_grpc::Image> local_images;
      {
        std::unique_lock<std::mutex> lock2(images_mutex);
        cond.wait_for(lock2, 1s);
        local_images.swap(images);
      }
      for (auto &i : local_images) {
        writer->Write(i);
      }
    }
    connection.disconnect();
    std::lock_guard<std::mutex> lock(connection_mutex);
    std::this_thread::sleep_for(1s);
  }

  return ::grpc::Status::OK;
}

::grpc::Status Service::notification(
    ::grpc::ServerContext *context, const ::thalamus_grpc::Empty *,
    ::grpc::ServerWriter<::thalamus_grpc::Notification> *writer) {
  impl->notification_writer = writer;
  THALAMUS_LOG(info) << "Notification stream received";
  while (!context->IsCancelled()) {
    std::this_thread::sleep_for(1s);
  }
  THALAMUS_LOG(info) << "Notification stream cancelled";
  impl->notification_writer = nullptr;
  return ::grpc::Status::OK;
}

::grpc::Status Service::ping(
    ::grpc::ServerContext *context,
    ::grpc::ServerReaderWriter<::thalamus_grpc::Pong, ::thalamus_grpc::Ping>
        *stream) {
  Impl::ContextGuard guard(this, context);
  thalamus_grpc::Ping ping;
  thalamus_grpc::Pong pong;
  while (stream->Read(&ping)) {
    pong.set_id(ping.id());
    *pong.mutable_payload() = *ping.mutable_payload();
    stream->Write(pong);
  }
  return ::grpc::Status::OK;
}

::grpc::Status Service::replay(::grpc::ServerContext *,
                               const ::thalamus_grpc::ReplayRequest *,
                               ::thalamus_grpc::Empty *) {
  return ::grpc::Status::OK;
}

::grpc::Status Service::eval(
    ::grpc::ServerContext *context,
    ::grpc::ServerReaderWriter<::thalamus_grpc::EvalRequest,
                               ::thalamus_grpc::EvalResponse> *stream) {
  set_current_thread_name("eval");
  Impl::ContextGuard guard(this, context);
  ::thalamus_grpc::EvalResponse response;
  impl->eval_stream = stream;
  while (stream->Read(&response)) {
    TRACE_EVENT("thalamus", "eval");
    std::unique_lock<std::mutex> lock(impl->mutex);
    auto i = impl->eval_promises.find(response.id());
    auto &promise = i->second;

    boost::json::value parsed = boost::json::parse(response.value());
    auto value = ObservableCollection::from_json(parsed);

    promise.set_value(value);
    TRACE_EVENT_END("thalamus", perfetto::Track(response.id()));
  }
  return ::grpc::Status::OK;
}

::grpc::Status Service::stim(
    ::grpc::ServerContext *context,
    ::grpc::ServerReaderWriter<::thalamus_grpc::StimResponse,
                               ::thalamus_grpc::StimRequest> *reader) {
  set_current_thread_name("stim");
  Impl::ContextGuard guard(this, context);
  ::thalamus_grpc::StimRequest request;
  thalamus_grpc::NodeSelector node_name;
  if (!reader->Read(&request)) {
    THALAMUS_LOG(error) << "Couldn't read name of node to inject into";
    return ::grpc::Status::OK;
  }
  if (!request.has_node()) {
    THALAMUS_LOG(error)
        << "First message of inject_analog request should contain node name";
    return ::grpc::Status::OK;
  }

  node_name = request.node();

  while (!context->IsCancelled()) {
    std::promise<void> promise;
    auto future = promise.get_future();
    std::shared_ptr<Node> raw_node;
    boost::asio::post(impl->io_context, [&] {
      impl->node_graph.get_node(node_name, [&](auto ptr) {
        raw_node = ptr.lock();
        promise.set_value();
      });
    });
    THALAMUS_LOG(info) << "Waiting for node";
    while (future.wait_for(1s) == std::future_status::timeout &&
           !context->IsCancelled()) {
      if (impl->io_context.stopped()) {
        return ::grpc::Status::OK;
      }
    }
    if (context->IsCancelled()) {
      continue;
    }
    THALAMUS_LOG(info) << "Got node";

    auto node = node_cast<StimNode *>(raw_node.get());
    THALAMUS_LOG(info) << "stimnode " << node;
    if (!node) {
      std::this_thread::sleep_for(1s);
      continue;
    }

    thalamus_grpc::StimResponse response;
    response.set_id(request.id());
    reader->Write(response);

    while (reader->Read(&request)) {
      if (request.has_node()) {
        node_name = request.node();
        break;
      }

      std::promise<void> response_promise;
      auto response_future = response_promise.get_future();
      std::future<thalamus_grpc::StimResponse> inner_future;
      auto id = request.id();
      boost::asio::post(impl->io_context, [&] {
        inner_future = node->stim(std::move(request));
        response_promise.set_value();
      });
      while (response_future.wait_for(1s) == std::future_status::timeout &&
             !context->IsCancelled()) {
      }
      if (!context->IsCancelled()) {
        response_future.get();
        auto next_response = inner_future.get();
        next_response.set_id(id);
        reader->Write(next_response);
      }
    }
  }
  return ::grpc::Status::OK;
}

std::future<ObservableCollection::Value>
Service::evaluate(const std::string &code) {
  auto id = get_unique_id();
  TRACE_EVENT_BEGIN("thalamus", "evaluate", perfetto::Track(id));

  thalamus_grpc::EvalRequest request;
  request.set_code(code);
  request.set_id(id);

  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->eval_promises[request.id()] =
        std::promise<ObservableCollection::Value>();
  }

  auto writer = impl->eval_stream.load();
  BOOST_ASSERT_MSG(writer != nullptr,
                   "Attempted to evaluate code with no stream");
  writer->Write(request);

  return impl->eval_promises[request.id()].get_future();
}

void Service::warn(const std::string &title, const std::string &message) {
  thalamus_grpc::Notification request;
  request.set_type(thalamus_grpc::Notification::Warning);
  request.set_title(title);
  request.set_message(message);

  auto writer = impl->notification_writer.load();
  BOOST_ASSERT_MSG(writer != nullptr,
                   "Attempted to send notification with no stream");
  writer->Write(request);
}

void Service::stop() {
  std::lock_guard<std::mutex> lock(impl->mutex);
  for (auto context : impl->contexts) {
    context->TryCancel();
  }
}

void Service::wait() {
  auto waiting = [&] {
    return impl->observable_bridge_stream.load() == nullptr ||
           impl->notification_writer.load() == nullptr;
  };
  while (waiting()) {
    THALAMUS_LOG(info) << "Waiting for state service";
    std::this_thread::sleep_for(1s);
  }
  // std::cout << "State service arrived" << std::endl;
}
} // namespace thalamus
