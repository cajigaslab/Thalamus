#include <grpc_impl.h>
#include <algorithm>
#include <h5handle.h>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/quat_access.hpp>
#include <image_node.h>
#include <text_node.h>
#include <kfr/all.hpp>

namespace thalamus {
  using namespace std::chrono_literals;
  using namespace std::placeholders;

#define SWAP(a,b) tempr=(a);(a)=(b);(b)=tempr
  void four1(double data[], unsigned long nn, int isign)
  {
    unsigned long n,mmax,m,j,istep,i;
    double wtemp,wr,wpr,wpi,wi,theta;
    double tempr, tempi;
    n=nn << 1;
    j=1;
    for (i=1;i<n;i+=2) {
      if (j > i) {
        SWAP(data[j],data[i]);
        SWAP(data[j+1],data[i+1]);
      }
      m=n >> 1;
      while (m >= 2 && j > m) {
        j -= m;
        m >>= 1;
      }
      j += m;
    }
    mmax=2;
    while (n > mmax) {
      istep=mmax << 1;
      theta=isign*(6.28318530717959/mmax);
      wtemp=sin(0.5*theta);
      wpr = -2.0*wtemp*wtemp;
      wpi=sin(theta);
      wr=1.0;
      wi=0.0;
      for (m=1;m<mmax;m+=2) {
        for (i=m;i<=n;i+=istep) {
          j=i+mmax;
          tempr=wr*data[j]-wi*data[j+1];
          tempi=wr*data[j+1]+wi*data[j];
          data[j]=data[i]-tempr;
          data[j+1]=data[i+1]-tempi;
          data[i] += tempr;
          data[i+1] += tempi;
        }
        wr=(wtemp=wr)*wpr-wi*wpi+wr;
        wi=wi*wpr+wtemp*wpi+wi;
      }
      mmax=istep;
    }
  }

  void realft(double data[], unsigned long n, int isign)
  {
    unsigned long i,i1,i2,i3,i4,np3;
    double c1=0.5,c2,h1r,h1i,h2r,h2i;
    double wr,wi,wpr,wpi,wtemp,theta;
    theta=3.141592653589793/(double) (n>>1);
    if (isign == 1) {
      c2 = -0.5;
      four1(data,n>>1,1);
    } else {
      c2=0.5;
      theta = -theta;
    }
    wtemp=sin(0.5*theta);
    wpr = -2.0*wtemp*wtemp;
    wpi=sin(theta);
    wr=1.0+wpr;
    wi=wpi;
    np3=n+3;
    for (i=2;i<=(n>>2);i++) {
      i4=1+(i3=np3-(i2=1+(i1=i+i-1)));
      h1r=c1*(data[i1]+data[i3]);
      h1i=c1*(data[i2]-data[i4]);
      h2r = -c2*(data[i2]+data[i4]);
      h2i=c2*(data[i1]-data[i3]);
      data[i1]=h1r+wr*h2r-wi*h2i;
      data[i2]=h1i+wr*h2i+wi*h2r;
      data[i3]=h1r-wr*h2r+wi*h2i;
      data[i4] = -h1i+wr*h2i+wi*h2r;
      wr=(wtemp=wr)*wpr-wi*wpi+wr;
      wi=wi*wpr+wtemp*wpi+wi;
    }
    if (isign == 1) {
      data[1] = (h1r=data[1])+data[2];
      data[2] = h1r-data[2];
    } else {
      data[1]=c1*((h1r=data[1])+data[2]);
      data[2]=c1*(h1r-data[2]);
      four1(data,n>>1,-1);
    }
  }

  ::grpc::Status StacktraceAndReturnStatusOnError(std::function<::grpc::Status()> func) {
    try {
      return func();
    }
    catch (std::exception const& e)
    {
      const boost::stacktrace::stacktrace* st = boost::get_error_info<thalamus::traced>(e);
      THALAMUS_LOG(fatal) << e.what() << "\n" << *st;
      return ::grpc::Status(grpc::INTERNAL, e.what());
    }
  }

  struct Service::Impl {
    boost::asio::io_context& io_context;
    ObservableCollection::Value state;
    std::atomic_ullong next_id;
    std::atomic<std::thread::id> observable_bridge_thread_id;
    std::atomic<::grpc::ServerReaderWriter< ::thalamus_grpc::ObservableChange, ::thalamus_grpc::ObservableChange>*> observable_bridge_stream;
    std::atomic<::grpc::ServerReaderWriter< ::thalamus_grpc::EvalRequest, ::thalamus_grpc::EvalResponse>*> eval_stream;
    std::atomic<::grpc::ServerReaderWriter< ::thalamus_grpc::EvalRequest, ::thalamus_grpc::EvalResponse>*> graph_stream;
    std::atomic<::grpc::ServerWriter< ::thalamus_grpc::Notification>*> notification_writer;
    std::map<unsigned long long, std::function<void()>> pending_changes;
    std::map<unsigned long long, std::promise<ObservableCollection::Value>> eval_promises;
    std::mutex mutex;
    std::set< ::grpc::ServerContext*> contexts;
    std::set<std::promise<void>*> promises;
    std::condition_variable condition;
    NodeGraph& node_graph;
    Service* outer;
    Impl(ObservableCollection::Value state, boost::asio::io_context& io_context, NodeGraph& node_graph, Service* outer)
      : io_context(io_context)
      , state(state)
      , next_id(1)
      , observable_bridge_stream(nullptr)
      , notification_writer(nullptr)
      , node_graph(node_graph)
      , outer(outer) {

      if (std::holds_alternative<ObservableListPtr>(state)) {
        auto temp = std::get<ObservableListPtr>(state);
        temp->set_remote_storage(std::bind(&Service::send_change, outer, _1, _2, _3, _4));
      }
      else if (std::holds_alternative<ObservableDictPtr>(state)) {
        auto temp = std::get<ObservableDictPtr>(state);
        temp->set_remote_storage(std::bind(&Service::send_change, outer, _1, _2, _3, _4));
      }
    }

    class ContextGuard {
    public:
      Service* service;
      ::grpc::ServerContext* context;
      ContextGuard(Service* service, ::grpc::ServerContext* context) : service(service), context(context) {
        std::lock_guard<std::mutex> lock(service->impl->mutex);
        service->impl->contexts.insert(context);
      }
      ~ContextGuard() {
        std::lock_guard<std::mutex> lock(service->impl->mutex);
        service->impl->contexts.erase(context);
      }
    };

    ::grpc::Status analog(::grpc::ServerContext* context, const::thalamus_grpc::AnalogRequest* request, std::function<bool(::thalamus_grpc::AnalogResponse& msg, ::grpc::WriteOptions options)> writer) {

      Impl::ContextGuard guard(outer, context);
      while (!context->IsCancelled()) {
        std::promise<void> promise;
        auto future = promise.get_future();
        std::shared_ptr<Node> raw_node;
        boost::asio::post(io_context, [&] {
          node_graph.get_node(request->node(), [&](auto ptr) {
            raw_node = ptr.lock();
            promise.set_value();
          });
        });
        while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
          if (io_context.stopped()) {
            ::thalamus_grpc::AnalogResponse response;
            ::grpc::WriteOptions options;
            options.set_last_message();
            writer(response, options);
            return ::grpc::Status::OK;
          }
        }
        if (!dynamic_cast<AnalogNode*>(raw_node.get())) {
          std::this_thread::sleep_for(1s);
          continue;
        }

        AnalogNode* node = dynamic_cast<AnalogNode*>(raw_node.get());
        std::vector<size_t> channels(request->channels().begin(), request->channels().end());
        thalamus::vector<std::string> channel_names(request->channel_names().begin(), request->channel_names().begin());
        auto has_channels = !channels.empty() || !channel_names.empty();

        std::mutex connection_mutex;

        using signal_type = decltype(raw_node->ready);
        auto connection = raw_node->ready.connect(signal_type::slot_type([&](const Node*) {
          if(!connection_mutex.try_lock()) {
            return;
          }
          std::lock_guard<std::mutex> lock(connection_mutex, std::adopt_lock_t());
          ::thalamus_grpc::AnalogResponse response;

          size_t num_channels = node->num_channels();
          if (!has_channels && channels.size() != num_channels) {
            for (auto i = channels.size(); i < num_channels; ++i) {
              channels.push_back(i);
            }
            channels.resize(num_channels);
          }

          if(!channel_names.empty()) {
            std::vector<int> named_channels;
            for(auto& name : channel_names) {
              for(auto i = 0;i < num_channels;++i) {
                if(node->name(i) == name) {
                  named_channels.push_back(i);
                  break;
                }
              }
            }
            if(named_channels.size() == channel_names.size()) {
              channels.insert(channels.end(), named_channels.begin(), named_channels.end());
              channel_names.clear();
            } else {
              return;
            }
          }

          for (auto c = 0u; c < channels.size(); ++c) {
            auto channel = channels[c];
            if (channel >= num_channels) {
              continue;
            }
            auto span = response.add_spans();
            span->set_begin(response.data_size());
            auto name = node->name(channel);
            span->set_name(name.data(), name.size());

            response.add_sample_intervals(node->sample_interval(channel).count());

            auto data = node->data(channel);
            response.mutable_data()->Add(data.begin(), data.end());

            span->set_end(response.data_size());
          }
          writer(response, ::grpc::WriteOptions());
          }).track_foreign(raw_node));
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
        connection.disconnect();
        std::lock_guard<std::mutex> lock(connection_mutex);
        std::this_thread::sleep_for(1s);
      }

      return ::grpc::Status::OK;
    }
  };

  Service::Service(ObservableCollection::Value state, boost::asio::io_context& io_context, NodeGraph& node_graph) : impl(new Impl(state, io_context, node_graph, this)) {}
  Service::~Service() {}

  ::grpc::Status Service::get_modalities(::grpc::ServerContext* context, const ::thalamus_grpc::NodeSelector* request, ::thalamus_grpc::ModalitiesMessage* response) {
    std::shared_ptr<Node> raw_node;
    std::promise<void> promise;
    auto future = promise.get_future();
    boost::asio::post(impl->io_context, [&] {
      impl->node_graph.get_node(*request, [&](auto ptr) {
        raw_node = ptr.lock();
        promise.set_value();
      });
    });
    while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
      if (impl->io_context.stopped()) {
        return ::grpc::Status::OK;
      }
    }
    if (dynamic_cast<AnalogNode*>(raw_node.get())) {
      response->add_values(thalamus_grpc::Modalities::AnalogModality);
    }
    if (dynamic_cast<MotionCaptureNode*>(raw_node.get())) {
      response->add_values(thalamus_grpc::Modalities::MocapModality);
    }
    if (dynamic_cast<ImageNode*>(raw_node.get())) {
      response->add_values(thalamus_grpc::Modalities::ImageModality);
    }
    if (dynamic_cast<TextNode*>(raw_node.get())) {
      response->add_values(thalamus_grpc::Modalities::TextModality);
    }
    return ::grpc::Status::OK;
  }

  ::grpc::Status Service::get_type_name(::grpc::ServerContext*, const ::thalamus_grpc::StringMessage* request, ::thalamus_grpc::StringMessage* response) {
    auto result = impl->node_graph.get_type_name(request->value());
    response->set_value(result.value_or(""));
    return ::grpc::Status::OK;
  }

  ::grpc::Status Service::get_recommended_channels(::grpc::ServerContext* context, const ::thalamus_grpc::NodeSelector* request, ::thalamus_grpc::StringListMessage* response) {
    Impl::ContextGuard guard(this, context);

    std::promise<void> promise;
    auto future = promise.get_future();
    std::shared_ptr<Node> raw_node;
    boost::asio::post(impl->io_context, [&] {
      impl->node_graph.get_node(*request, [&](auto ptr) {
        raw_node = ptr.lock();
        AnalogNode* node = dynamic_cast<AnalogNode*>(raw_node.get());
        if (!node) {
          promise.set_value();
          return;
        }
        auto values = node->get_recommended_channels();
        for(const auto& value : values) {
          response->add_value(value);
        }
        promise.set_value();
      });
    });
    while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
      if (impl->io_context.stopped()) {
        return ::grpc::Status::OK;
      }
    }
    return ::grpc::Status::OK;
  }

  ::grpc::Status Service::node_request(::grpc::ServerContext*, const ::thalamus_grpc::NodeRequest* request, ::thalamus_grpc::NodeResponse* response) {
    auto weak = impl->node_graph.get_node(request->node());
    auto node = weak.lock();
    if(!node) {
      return ::grpc::Status::OK;
    }

    auto parsed = boost::json::parse(request->json());
    auto json_response = node->process(parsed);
    auto serialized_response = boost::json::serialize(json_response);
    response->set_json(serialized_response);

    return ::grpc::Status::OK;
  }

  ::grpc::Status Service::events(::grpc::ServerContext* context, ::grpc::ServerReader< ::thalamus_grpc::Event>* reader, ::util_grpc::Empty*) {
    tracing::SetCurrentThreadName("events");
    Impl::ContextGuard guard(this, context);
    ::thalamus_grpc::Event the_event;
    while (reader->Read(&the_event)) {
      TRACE_EVENT0("thalamus", "events");
      std::promise<void> promise;
      auto future = promise.get_future();
      boost::asio::post(impl->io_context, [&] {
        TRACE_EVENT0("thalamus", "events(post)");
        events_signal(the_event);
        promise.set_value();
        });
      while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {}
    }
    return ::grpc::Status::OK;
  }


  ::grpc::Status Service::observable_bridge(::grpc::ServerContext* context, ::grpc::ServerReaderWriter< ::thalamus_grpc::ObservableChange, ::thalamus_grpc::ObservableChange>* stream) {
    tracing::SetCurrentThreadName("observable_bridge");
    Impl::ContextGuard guard(this, context);
    impl->observable_bridge_thread_id = std::this_thread::get_id();
    impl->observable_bridge_stream = stream;
    thalamus_grpc::ObservableChange change;
    while (stream->Read(&change)) {
      TRACE_EVENT0("thalamus", "observable_bridge");
      //std::cout << change.address() << " " << change.value() << "ACK: " << change.acknowledged() << std::endl;
      if (change.acknowledged()) {
        std::function<void()> callback;
        {
          std::unique_lock<std::mutex> lock(impl->mutex);
          callback = impl->pending_changes.at(change.acknowledged());
          TRACE_EVENT_ASYNC_END0("thalamus", "send_change", change.acknowledged());
          impl->pending_changes.erase(change.acknowledged());
        }
        boost::asio::post(impl->io_context, callback);
        continue;
      }
      THALAMUS_LOG(trace) << change.address() << " " << change.value() << std::endl;

      boost::json::value parsed = boost::json::parse(change.value());
      auto value = ObservableCollection::from_json(parsed);

      std::promise<void> promise;
      auto future = promise.get_future();
      boost::asio::post(impl->io_context, [&] {
        TRACE_EVENT0("thalamus", "observable_bridge(post)");
        //change_signal(change);
        if (change.action() == thalamus_grpc::ObservableChange_Action_Set) {
          set_jsonpath(impl->state, change.address(), value, true);
        }
        else {
          delete_jsonpath(impl->state, change.address(), true);
        }
        promise.set_value();
        });
      while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {}
    }

    impl->observable_bridge_stream = nullptr;
    impl->io_context.stop();
    return ::grpc::Status::OK;
  }

  ::grpc::Status Service::inject_analog(::grpc::ServerContext* context, ::grpc::ServerReader< ::thalamus_grpc::InjectAnalogRequest>* reader, ::util_grpc::Empty*) {
    tracing::SetCurrentThreadName("inject_analog");
    Impl::ContextGuard guard(this, context);
    ::thalamus_grpc::InjectAnalogRequest request;
    std::string node_name;
    if(!reader->Read(&request)) {
      THALAMUS_LOG(error) << "Couldn't read name of node to inject into";
      return ::grpc::Status::OK;
    }
    if(!request.has_node()) {
      THALAMUS_LOG(error) << "First message of inject_analog request should contain node name";
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
      while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
        if (impl->io_context.stopped()) {
          return ::grpc::Status::OK;
        }
      }
      if(context->IsCancelled()) {
        continue;
      }
      THALAMUS_LOG(info) << "Got node";

      AnalogNode* node = dynamic_cast<AnalogNode*>(raw_node.get());
      if (!node) {
        std::this_thread::sleep_for(1s);
        continue;
      }

      thalamus::vector<std::span<const double>> spans;
      thalamus::vector<std::chrono::nanoseconds> sample_intervals;
      thalamus::vector<std::string_view> names;
      while(reader->Read(&request)) {
        if(request.has_node()) {
          node_name = request.node();
          break;
        }
        
        auto& data = request.signal().data();
        spans.clear();
        names.clear();
        for(auto& span : request.signal().spans()) {
          spans.emplace_back(data.begin() + span.begin(), data.begin() + span.end());
          names.emplace_back(span.name().begin(), span.name().end());
        }
        sample_intervals.clear();
        for(auto& interval : request.signal().sample_intervals()) {
          sample_intervals.emplace_back(interval);
        }

        std::promise<void> promise;
        auto future = promise.get_future();
        boost::asio::post(impl->io_context, [&] {
          node->inject(spans, sample_intervals, names);
          promise.set_value();
        });
        while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {}
      }
    }
    return ::grpc::Status::OK;
  }

  struct Counter {
    static std::atomic_size_t count;
    std::string label;
    Counter(const std::string& label) : label(label) {
      THALAMUS_LOG(info) << label << " add " << ++count;
    }
    ~Counter() {
      THALAMUS_LOG(info) << label << " remove " << --count;
    }
  };
  std::atomic_size_t Counter::count = 0;

  ::grpc::Status Service::graph(::grpc::ServerContext* context, const ::thalamus_grpc::GraphRequest* request, ::grpc::ServerWriter< ::thalamus_grpc::GraphResponse>* writer) {
    Impl::ContextGuard guard(this, context);
    std::stringstream stream;
    stream << request->node().name();
    for(auto& name : request->channel_names()) {
      stream << " "  << name;
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
      while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
        if (impl->io_context.stopped()) {
          writer->WriteLast(::thalamus_grpc::GraphResponse(), ::grpc::WriteOptions());
          return ::grpc::Status::OK;
        }
      }
      if (!dynamic_cast<AnalogNode*>(raw_node.get())) {
        std::this_thread::sleep_for(1s);
        continue;
      }

      AnalogNode* node = dynamic_cast<AnalogNode*>(raw_node.get());
      std::vector<size_t> channels(request->channels().begin(), request->channels().end());

      std::chrono::nanoseconds bin_ns(request->bin_ns());
      std::mutex connection_mutex;
      std::vector<double> mins(channels.size(), std::numeric_limits<double>::max());
      std::vector<double> maxs(channels.size(), -std::numeric_limits<double>::max());
      std::vector<double> previous_mins(channels.size(), 0);
      std::vector<double> previous_maxes(channels.size(), 0);
      std::vector<std::chrono::nanoseconds> current_times(channels.size());
      std::vector<std::chrono::nanoseconds> bin_ends(channels.size(), bin_ns);
      thalamus::vector<std::string> channel_names(request->channel_names().begin(), request->channel_names().end());

      auto has_channels = !channels.empty() || !channel_names.empty();

      using signal_type = decltype(raw_node->ready);
      auto connection = raw_node->ready.connect(signal_type::slot_type([&](const Node*) {
        if (!node->has_analog_data()) {
          return;
        }
        std::lock_guard<std::mutex> lock(connection_mutex);
        ::thalamus_grpc::GraphResponse response;
        size_t num_channels = node->num_channels();
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

        if(!channel_names.empty()) {
          std::vector<int> named_channels;
          for(auto& name : channel_names) {
            for(auto i = 0;i < num_channels;++i) {
              if(node->name(i) == name) {
                named_channels.push_back(i);
                break;
              }
            }
          }
          if(named_channels.size() == channel_names.size()) {
            channels.insert(channels.end(), named_channels.begin(), named_channels.end());
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

        for (auto c = 0u; c < channels.size(); ++c) {
          auto channel = channels[c];
          auto& min = mins[c];
          auto& max = maxs[c];
          auto& previous_min = previous_mins[c];
          auto& previous_max = previous_maxes[c];
          auto& current_time = current_times[c];
          auto& bin_end = bin_ends[c];
          if (channel >= num_channels) {
            continue;
          }
          auto span = response.add_spans();
          span->set_begin(response.bins_size());

          auto interval = node->sample_interval(channel);
          auto data = node->data(channel);
          for (auto sample : data) {
            auto wrote = current_time >= bin_end;
            while (current_time >= bin_end) {
              response.add_bins(min);
              response.add_bins(max);
              bin_end += bin_ns;
            }
            if(wrote) {
              min = std::numeric_limits<double>::max();
              max = -std::numeric_limits<double>::max();
            }
            min = std::min(min, sample);
            max = std::max(max, sample);
            current_time += interval;
          }
          span->set_end(response.bins_size());
          auto name = node->name(channel);
          span->set_name(name.data(), name.size());
        }
        writer->Write(response);
        }).track_foreign(raw_node));
      raw_node.reset();

      while (!context->IsCancelled() && connection.connected()) {
        if (impl->io_context.stopped()) {
          writer->WriteLast(::thalamus_grpc::GraphResponse(), ::grpc::WriteOptions());
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

  ::grpc::Status Service::channel_info(::grpc::ServerContext* context, const::thalamus_grpc::AnalogRequest* request, ::grpc::ServerWriter< ::thalamus_grpc::AnalogResponse>* writer) {
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
      while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
        if (impl->io_context.stopped()) {
          ::thalamus_grpc::AnalogResponse response;
          ::grpc::WriteOptions options;
          options.set_last_message();
          writer->Write(response, options);
          return ::grpc::Status::OK;
        }
      }
      auto raw_node = weak_raw_node.lock();
      if (!dynamic_cast<AnalogNode*>(raw_node.get())) {
        std::this_thread::sleep_for(1s);
        continue;
      }

      AnalogNode* node = dynamic_cast<AnalogNode*>(raw_node.get());
      std::vector<size_t> channels(request->channels().begin(), request->channels().end());
      thalamus::vector<std::string> channel_names(request->channel_names().begin(), request->channel_names().begin());
      auto has_channels = !channels.empty() || !channel_names.empty();

      std::mutex connection_mutex;
      std::mutex cond_mutex;
      std::condition_variable cond;
      bool channels_changed = true;

      using channels_changed_signal_type = decltype(node->channels_changed);
      using signal_type = decltype(raw_node->ready);

      boost::signals2::scoped_connection channels_connection = node->channels_changed.connect(channels_changed_signal_type::slot_type([&](const AnalogNode*) {
        if(!connection_mutex.try_lock()) {
          return;
        }
        std::lock_guard<std::mutex> lock(connection_mutex, std::adopt_lock_t());
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
        if(!raw_node) {
          break;
        }

        boost::signals2::scoped_connection connection = raw_node->ready.connect(signal_type::slot_type([&](const Node*) {
          if(!connection_mutex.try_lock()) {
            return;
          }
          std::lock_guard<std::mutex> lock(connection_mutex, std::adopt_lock_t());
          ::thalamus_grpc::AnalogResponse response;

          for (auto c = 0u; c < node->num_channels(); ++c) {
            auto span = response.add_spans();
            auto name = node->name(c);
            span->set_name(name.data(), name.size());
            response.add_sample_intervals(node->sample_interval(c).count());
          }
          writer->Write(response, ::grpc::WriteOptions());

          {
            std::lock_guard<std::mutex> lock(info_mutex);
            got_info = true;
          }
          info_cond.notify_one();
          connection.disconnect();
        }));
        raw_node.reset();

        {
          std::unique_lock<std::mutex> lock2(info_mutex);
          info_cond.wait(lock2, [&] { return context->IsCancelled() || !connection.connected() || impl->io_context.stopped() || got_info; });
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

  ::grpc::Status Service::spectrogram(::grpc::ServerContext* context, const ::thalamus_grpc::SpectrogramRequest* request, ::grpc::ServerWriter< ::thalamus_grpc::SpectrogramResponse>* writer) {
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
      while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
        if (impl->io_context.stopped()) {
          writer->WriteLast(::thalamus_grpc::SpectrogramResponse(), ::grpc::WriteOptions());
          return ::grpc::Status::OK;
        }
      }
      if (!dynamic_cast<AnalogNode*>(raw_node.get())) {
        std::this_thread::sleep_for(1s);
        continue;
      }

      AnalogNode* node = dynamic_cast<AnalogNode*>(raw_node.get());
      std::vector<kfr::univector<double>> accumulated_data(request->channels().size());
      std::chrono::nanoseconds window_ns(static_cast<size_t>(request->window_s()*1e9));
      std::vector<int> window_samples(request->channels().size());
      std::chrono::nanoseconds hop_ns(static_cast<size_t>(request->hop_s()*1e9));
      std::map<int, kfr::univector<double>> windows;
      std::map<int, int> window_sizes;

      std::mutex connection_mutex;
      std::vector<std::chrono::nanoseconds> countdowns;

      std::set<int> channel_ids_set;
      std::vector<int> channel_ids;
      std::set<std::string> unlocated_channels;
      for(auto& c : request->channels()) {
        if(!c.name().empty()) {
          unlocated_channels.insert(c.name());
        } else {
          channel_ids_set.insert(c.index());
        }
        channel_ids.assign(channel_ids_set.begin(), channel_ids_set.end());
      }

      std::vector<thalamus_grpc::ChannelId> output_channel_ids;

      using signal_type = decltype(raw_node->ready);
      auto connection = raw_node->ready.connect(signal_type::slot_type([&](const Node*) {
        if (!node->has_analog_data()) {
          return;
        }
        if(!connection_mutex.try_lock()) {
          return;
        }
        std::lock_guard<std::mutex> lock(connection_mutex, std::adopt_lock_t());
        size_t num_channels = node->num_channels();

        if(!unlocated_channels.empty()) {
          for(auto i = 0;i < num_channels;++i) {
            auto name_view = node->name(i);
            std::string name(name_view.begin(), name_view.end());
            if(unlocated_channels.contains(name)) {
              channel_ids_set.insert(i);
              unlocated_channels.erase(name);
            }
          }
          channel_ids.assign(channel_ids_set.begin(), channel_ids_set.end());
        }

        if(!unlocated_channels.empty()) {
          return;
        }

        countdowns.resize(num_channels);

        for (auto c = 0u; c < channel_ids.size(); ++c) {
          auto channel = channel_ids[c];
          auto data = node->data(channel);
          auto interval = node->sample_interval(channel);
          auto& countdown = countdowns[channel];
          auto skips = countdown/interval;
          if(skips > data.size()) {
            skips = data.size();
          }

          auto& accumulated_channel = accumulated_data.at(channel);
          accumulated_channel.insert(accumulated_channel.end(), data.begin() + skips, data.end());
          countdown -= skips * interval;
        }

        auto working = true;
        while(working) {
          working = false;
          ::thalamus_grpc::SpectrogramResponse response;

          for (auto c = 0u; c < channel_ids.size(); ++c) {
            auto channel = channel_ids[c];
            auto interval = node->sample_interval(channel);
            auto data = node->data(channel);
            auto name = node->name(channel);
            auto& countdown = countdowns[channel];
            if(countdown >= interval) {
              continue;
            }

            auto window_samples = window_ns/interval;
            if(!window_sizes.contains(window_samples)) {
              auto i = 1;
              while(i < window_samples) {
                i <<= 1;
              }
              window_sizes[window_samples] = i;
            }
            auto window_size = window_sizes[window_samples];

            auto& accumulated_channel = accumulated_data.at(channel);
            if(accumulated_channel.size() >= window_size) {
              auto samples = window_size;
              if(!windows.contains(samples)) {
                windows[samples] = kfr::window_hamming(samples);
              }
              auto& window = windows.at(samples);
              std::vector<double> accumulated_copy(1);
              accumulated_copy.insert(accumulated_copy.end(), accumulated_channel.begin(), accumulated_channel.begin()+samples);

              for(auto j = 0;j < window.size();++j) {
                accumulated_copy[1+j] *= window[j];
              }
              realft(accumulated_copy.data(), accumulated_copy.size()-1, 1);

              auto spectrogram = response.add_spectrograms();
              spectrogram->mutable_channel()->set_index(channel);
              spectrogram->mutable_channel()->set_name(name.data(), name.size());
              spectrogram->set_max_frequency(.5e9/interval.count());

              spectrogram->mutable_data()->Add(accumulated_copy.begin()+1, accumulated_copy.end());

              auto skips = hop_ns/interval;
              if(skips == 0) {
                skips = 1;
              }
              auto end = std::min(accumulated_channel.begin() + skips, accumulated_channel.end());
              auto count = std::distance(accumulated_channel.begin(), end);
              accumulated_channel.erase(accumulated_channel.begin(), end);
              countdown = std::max(hop_ns - count*interval, 0ns);
            }
          }
          working = response.spectrograms_size() > 0;
          if(working) {
            writer->Write(response);
          }
        }
      }));
      raw_node.reset();

      while (!context->IsCancelled() && connection.connected()) {
        if (impl->io_context.stopped()) {
          writer->WriteLast(::thalamus_grpc::SpectrogramResponse(), ::grpc::WriteOptions());
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

  ::grpc::Status Service::analog(::grpc::ServerContext* context, const ::thalamus_grpc::AnalogRequest* request, ::grpc::ServerWriter< ::thalamus_grpc::AnalogResponse>* writer) {
    return impl->analog(context, request, [&](const ::thalamus_grpc::AnalogResponse & msg, const ::grpc::WriteOptions& options) {
      return writer->Write(msg, options);
    });
  }

  ::grpc::Status Service::remote_node(::grpc::ServerContext* context, ::grpc::ServerReaderWriter< ::thalamus_grpc::RemoteNodeMessage, ::thalamus_grpc::RemoteNodeMessage>* stream) {
    ::thalamus_grpc::RemoteNodeMessage message;
    stream->Read(&message);
    auto request = message.release_request();

    std::thread ping_thread([&] {
      while (stream->Read(&message)) {
        THALAMUS_ASSERT(message.has_ping());
        auto ping = message.ping();

        ::thalamus_grpc::RemoteNodeMessage response;
        auto pong = response.mutable_pong();
        pong->set_id(ping.id());

        stream->Write(response);
      }
    });

    auto result = impl->analog(context, request, [&](::thalamus_grpc::AnalogResponse& msg, const ::grpc::WriteOptions& options) {
      ::thalamus_grpc::RemoteNodeMessage response;
      response.mutable_data()->Swap(&msg);
      return stream->Write(response, options);
    });

    ping_thread.join();
    return result;
  }

  ::grpc::Status Service::xsens(::grpc::ServerContext* context, const ::thalamus_grpc::NodeSelector* request, ::grpc::ServerWriter< ::thalamus_grpc::XsensResponse>* writer) {
    Impl::ContextGuard guard(this, context);
    while (!context->IsCancelled()) {
      std::promise<void> promise;
      auto future = promise.get_future();
      std::shared_ptr<Node> raw_node;
      boost::asio::post(impl->io_context, [&] {
        impl->node_graph.get_node(*request, [&](auto ptr) {
          raw_node = ptr.lock();
          promise.set_value();
          });
        });
      while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
        if (impl->io_context.stopped()) {
          writer->WriteLast(::thalamus_grpc::XsensResponse(), ::grpc::WriteOptions());
          return ::grpc::Status::OK;
        }
      }
      if (!dynamic_cast<MotionCaptureNode*>(raw_node.get())) {
        std::this_thread::sleep_for(1s);
        continue;
      }

      auto node = dynamic_cast<MotionCaptureNode*>(raw_node.get());

      std::mutex connection_mutex;

      using signal_type = decltype(raw_node->ready);
      auto connection = raw_node->ready.connect(signal_type::slot_type([&](const Node*) {
        std::lock_guard<std::mutex> lock(connection_mutex);
        ::thalamus_grpc::XsensResponse response;
        response.set_pose_name(node->pose_name());
        auto data = node->segments();
        for (auto& segment : data) {
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
          writer->WriteLast(::thalamus_grpc::XsensResponse(), ::grpc::WriteOptions());
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

  static const size_t image_chunk_size = 524288;

  ::grpc::Status Service::image(::grpc::ServerContext* context, const ::thalamus_grpc::ImageRequest* request, ::grpc::ServerWriter< ::thalamus_grpc::Image>* writer) {
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
      while (future.wait_for(1s) == std::future_status::timeout && !context->IsCancelled()) {
        if (impl->io_context.stopped()) {
          writer->WriteLast(::thalamus_grpc::Image(), ::grpc::WriteOptions());
          return ::grpc::Status::OK;
        }
      }
      if (!dynamic_cast<ImageNode*>(raw_node.get())) {
        std::this_thread::sleep_for(1s);
        continue;
      }

      auto node = dynamic_cast<ImageNode*>(raw_node.get());

      std::mutex connection_mutex;
      std::mutex images_mutex;
      std::condition_variable cond;
      std::vector<thalamus_grpc::Image> images;
      std::vector<std::chrono::steady_clock::time_point> frame_times;

      using signal_type = decltype(raw_node->ready);
      auto connection = raw_node->ready.connect(signal_type::slot_type([&](const Node*) {
        std::lock_guard<std::mutex> lock(connection_mutex);
        std::vector<::thalamus_grpc::Image> responses;
        size_t position = 0;

        if(request->framerate()) {
          auto now = std::chrono::steady_clock::now();
          while (!frame_times.empty() && now - frame_times.front() >= 1s) {
            std::pop_heap(frame_times.begin(), frame_times.end(), [](auto& l, auto& r) { return l > r; });
            frame_times.pop_back();
          }
          if (!frame_times.empty()) {
            auto duration = now - frame_times.front();
            auto duration_seconds = double(duration.count())/decltype(duration)::period::den;
            if (frame_times.size()/duration_seconds >= request->framerate()) {
              return;
            }
          }

          frame_times.push_back(now);
          std::push_heap(frame_times.begin(), frame_times.end(), [](auto& l, auto& r) { return l > r; });
        }

        size_t data_count = 0;
        for(auto i = 0;i < node->num_planes();++i) {
          auto data = node->plane(i);
          data_count += data.size();
        }

        auto width = node->width();
        auto height = node->height();
        thalamus_grpc::Image::Format format;
        switch(node->format()) {
        case ImageNode::Format::Gray:
          format = thalamus_grpc::Image::Format::Image_Format_Gray;
          break;
        case ImageNode::Format::RGB:
          format = thalamus_grpc::Image::Format::Image_Format_RGB;
          break;
        case ImageNode::Format::YUYV422:
          format = thalamus_grpc::Image::Format::Image_Format_YUYV422;
          break;
        }

        while(position < data_count) {
          responses.emplace_back();
          auto& piece = responses.back();
          piece.set_width(width);
          piece.set_height(height);
          piece.set_format(format);
          
          size_t plane_offset = 0;
          size_t remaining_chunk = image_chunk_size;
          for(auto i = 0;i < node->num_planes();++i) {
            auto data = node->plane(i);
            if(position > plane_offset + data.size() || !remaining_chunk) {
              piece.add_data();
            } else {
              auto in_plane_offset = position - plane_offset;
              auto count = std::min(data.size() - in_plane_offset, remaining_chunk);
              piece.add_data(data.data() + in_plane_offset, count);
              remaining_chunk -= count;
            }
            plane_offset += data.size();
          }
          position += image_chunk_size - remaining_chunk;
        }
        responses.back().set_last(true);


        std::lock_guard<std::mutex> lock2(images_mutex);
        for(auto& i : responses) {
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
        for(auto& i : local_images) {
          writer->Write(i);
        }
      }
      connection.disconnect();
      std::lock_guard<std::mutex> lock(connection_mutex);
      std::this_thread::sleep_for(1s);
    }

    return ::grpc::Status::OK;
  }

  ::grpc::Status Service::notification(::grpc::ServerContext* context, const ::util_grpc::Empty* request, ::grpc::ServerWriter< ::thalamus_grpc::Notification>* writer) {
    impl->notification_writer = writer;
    THALAMUS_LOG(info) << "Notification stream received";
    while(!context->IsCancelled()) {
      std::this_thread::sleep_for(1s);
    }
    THALAMUS_LOG(info) << "Notification stream cancelled";
    impl->notification_writer = nullptr;
    return ::grpc::Status::OK;
  }

  ::grpc::Status Service::ping(::grpc::ServerContext* context, ::grpc::ServerReaderWriter< ::thalamus_grpc::Pong, ::thalamus_grpc::Ping>* stream) {
    Impl::ContextGuard guard(this, context);
    thalamus_grpc::Ping ping;
    thalamus_grpc::Pong pong;
    while(stream->Read(&ping)) {
      pong.set_id(ping.id());
      *pong.mutable_payload() = *ping.mutable_payload();
      stream->Write(pong);
    }
    return ::grpc::Status::OK;
  }

  class H5Node {
  public:
    enum class Type {
      ANALOG,
      XSENS,
    };
    std::string name;
    H5Handle data;
    H5Handle received;
    const hsize_t chunk_size = 10;
    thalamus::vector<size_t> received_loaded;
    hsize_t received_loaded_offset = chunk_size;
    hsize_t received_offset[2] = { 0,0 };
    thalamus::vector<thalamus::vector<double>> data_loaded;
    thalamus::vector<thalamus::vector<std::span<double const>>> spans;
    thalamus::vector<MotionCaptureNode::Segment> xsens_data_loaded;
    thalamus::vector<std::span<MotionCaptureNode::Segment const>> xsens_spans;
    thalamus::vector<size_t> data_offsets;
    thalamus::vector<size_t> data_consumed_offsets;
    hsize_t count[2] = { 0,0 };
    hsize_t received_length;
    size_t data_count[2] = { 0, 0 };
    size_t data_length;
    H5Handle mem_space;
    H5Handle file_space;
    H5Handle file_data_space;
    H5Handle segment_type;
    H5Handle group;
    Type type;
    Node* graph_node = nullptr;
    bool sample_intervals_loaded = false;
    thalamus::vector<std::chrono::nanoseconds> sample_intervals;
    H5Node()
      : received_loaded(chunk_size)
    {}

    void set_group(H5Handle group) {
      this->group = group;

      auto h5_status = H5Aexists(group, "Sample Interval");
      THALAMUS_ASSERT(h5_status >= 0, "H5Aexists failed");
      if (!h5_status) {
        return;
      }

      H5Handle sample_interval_attribute = H5Aopen(group, "Sample Interval", H5P_DEFAULT);
      THALAMUS_ASSERT(sample_interval_attribute, "H5Aopen failed");

      H5Handle space = H5Aget_space(sample_interval_attribute);
      hsize_t count;
      auto ndims = H5Sget_simple_extent_dims(space, &count, nullptr);
      THALAMUS_ASSERT(ndims >= 0, "H5Sget_simple_extent_dims failed");

      thalamus::vector<size_t> temp(ndims ? count : 1, 0);
      h5_status = H5Aread(sample_interval_attribute, H5T_NATIVE_UINT64, temp.data());
      THALAMUS_ASSERT(h5_status >= 0, "H5Aread failed");
      std::transform(temp.begin(), temp.end(), std::back_inserter(sample_intervals), [](auto arg) {
        return std::chrono::nanoseconds(arg);
        });
      sample_intervals_loaded = true;
    }

    void set_received(H5Handle received) {
      this->received = received;

      file_space = H5Dget_space(received);
      THALAMUS_ASSERT(file_space, "H5Dget_space failed");

      auto ndims = H5Sget_simple_extent_ndims(file_space);
      THALAMUS_ASSERT(ndims > 0, "H5Sget_simple_extent_ndims failed");
      if (ndims == 1) {
        return;
      }

      thalamus::vector<hsize_t> dims(ndims);
      thalamus::vector<hsize_t> maxdims(ndims);
      ndims = H5Sget_simple_extent_dims(file_space, dims.data(), maxdims.data());
      THALAMUS_ASSERT(ndims > 0, "H5Sget_simple_extent_dims failed");

      hsize_t mem_dims[] = { chunk_size, dims.at(1) };
      mem_space = H5Screate_simple(2, mem_dims, NULL);
      THALAMUS_ASSERT(mem_space, "H5Screate_simple");

      received_length = dims.at(0);
      count[1] = dims[1];
      received_loaded.resize(count[1] * chunk_size);
    }

    void set_data(H5Handle data) {
      this->data = data;

      file_data_space = H5Dget_space(data);
      THALAMUS_ASSERT(file_data_space, "H5Dget_space failed");

      auto ndims = H5Sget_simple_extent_ndims(file_data_space);
      THALAMUS_ASSERT(ndims > 0, "H5Sget_simple_extent_ndims failed");
      if (ndims == 1) {
        return;
      }

      thalamus::vector<hsize_t> dims(ndims);
      thalamus::vector<hsize_t> maxdims(ndims);
      ndims = H5Sget_simple_extent_dims(file_data_space, dims.data(), maxdims.data());
      THALAMUS_ASSERT(ndims > 0, "H5Sget_simple_extent_dims failed");

      data_length = dims.at(0);
      data_count[1] = dims.at(1);
      data_consumed_offsets.assign(dims.at(1), 0);
      data_offsets.assign(dims.at(1), 0);
      data_loaded.resize(dims.at(1));
    }

    thalamus::optional<std::chrono::nanoseconds> next_received() {
      if (received_loaded_offset * count[1] == received_loaded.size()) {
        count[0] = std::min(chunk_size, received_length - received_offset[0]);
        if (!count[0]) {
          return std::nullopt;
        }
        auto h5_status = H5Sselect_hyperslab(file_space, H5S_SELECT_SET, received_offset, NULL, count, NULL);
        BOOST_ASSERT(h5_status >= 0);

        if (count[0] < chunk_size) {
          mem_space = H5Screate_simple(2, count, NULL);
          THALAMUS_ASSERT(mem_space, "H5Screate_simple failed");
          received_loaded.resize(count[0] * count[1]);
        }
        h5_status = H5Dread(received, H5T_NATIVE_UINT64, mem_space, file_space, H5P_DEFAULT, received_loaded.data());
        BOOST_ASSERT(h5_status >= 0);

        received_loaded_offset = 0;
        received_offset[0] += count[0];
        if (type == Type::ANALOG) {
          load_analog_data();
        }
        else {
          load_xsens_data();
        }
      }

      return std::chrono::nanoseconds(received_loaded.at(received_loaded_offset * count[1]));
    }

    void pop_received() {
      received_loaded_offset = std::min(received_loaded_offset + 1, count[0]);
    }

    void load_xsens_data() {
      xsens_spans.assign(received_loaded.size(), std::span<MotionCaptureNode::Segment const>());

      auto channel = 0u;
      auto end = received_loaded.at((count[0] - 1) * count[1] + 1 + channel);

      hsize_t offset[] = { data_offsets[channel], channel };
      hsize_t count[] = { end - data_offsets[channel], 1 };
      auto h5_status = H5Sselect_hyperslab(file_data_space, H5S_SELECT_SET, offset, NULL, count, NULL);
      THALAMUS_ASSERT(h5_status >= 0, "H5Sselect_hyperslab failed");

      H5Handle mem_data_space = H5Screate_simple(1, count, NULL);
      THALAMUS_ASSERT(mem_data_space, "H5Screate_simple failed");

      xsens_data_loaded.resize(count[0]);
      h5_status = H5Dread(data, segment_type, mem_data_space, file_data_space, H5P_DEFAULT, xsens_data_loaded.data());
      THALAMUS_ASSERT(h5_status >= 0, "H5Dread failed");

      auto last = 0;
      for (auto i = 0u; i < this->count[0]; ++i) {
        auto end = received_loaded.at(i * this->count[1] + 1 + channel) - data_offsets.at(channel);
        xsens_spans.at(i) = std::span(xsens_data_loaded.begin() + last, xsens_data_loaded.begin() + end);
        last = end;
      }
      data_offsets.at(channel) += count[0];
    }

    void load_analog_data() {
      spans.assign(received_loaded.size(), thalamus::vector<std::span<double const>>(data_count[1]));
      for (auto channel = 0u; channel < data_count[1]; ++channel) {
        auto end = received_loaded.at((count[0] - 1) * this->count[1] + 1 + channel);

        hsize_t offset[] = { data_offsets[channel], channel };
        hsize_t count[] = { end - data_offsets[channel], 1 };
        auto h5_status = H5Sselect_hyperslab(file_data_space, H5S_SELECT_SET, offset, NULL, count, NULL);
        THALAMUS_ASSERT(h5_status >= 0, "H5Sselect_hyperslab failed");

        H5Handle mem_data_space = H5Screate_simple(1, count, NULL);
        THALAMUS_ASSERT(mem_data_space, "H5Screate_simple failed");

        auto& buffer = data_loaded.at(channel);
        buffer.resize(count[0]);
        h5_status = H5Dread(data, H5T_NATIVE_DOUBLE, mem_data_space, file_data_space, H5P_DEFAULT, buffer.data());
        THALAMUS_ASSERT(h5_status >= 0, "H5Dread failed");

        auto last = 0;
        for (auto i = 0u; i < this->count[0]; ++i) {
          auto end = received_loaded.at(i * this->count[1] + 1 + channel) - data_offsets.at(channel);
          spans.at(i).at(channel) = std::span(buffer.begin() + last, buffer.begin() + end);
          last = end;
        }
        data_offsets.at(channel) += count[0];
      }
      if (!sample_intervals_loaded) {
        sample_intervals.resize(data_count[1]);
        auto start = std::chrono::nanoseconds(received_loaded.at(0));
        auto end = std::chrono::nanoseconds(received_loaded.at((count[0] - 1) * count[1]));
        auto duration = end - start;
        for (auto channel = 0u; channel < data_count[1]; ++channel) {
          auto start_index = received_loaded.at(1 + channel);
          auto& buffer = data_loaded.at(channel);
          auto sample_interval = duration / (buffer.size() - start_index);
          sample_intervals.at(channel) = sample_interval;
        }
      }
    }

    thalamus::vector<std::span<double const>>& get_data() {
      return spans.at(received_loaded_offset);
    }

    std::span<MotionCaptureNode::Segment const>& get_xsens_data() {
      return xsens_spans.at(received_loaded_offset);
    }
  };

  //static herr_t read_dataset(hid_t group_id, const char* name, const H5L_info2_t* linfo, void* opdata) {
  //  std::map<std::string, H5Handle>& results = *static_cast<std::map<std::string, H5Handle>*>(opdata);
  //  H5Handle dataset = H5Dopen2(group_id, name, H5P_DEFAULT);
  //  results[name] = dataset;
  //  return 0;
  //}

  static herr_t read_node(hid_t group_id, const char* name, const H5L_info2_t*, void* opdata) {
    H5Node& result = *static_cast<H5Node*>(opdata);
    H5Handle dataset = H5Dopen2(group_id, name, H5P_DEFAULT);
    if (!dataset) {
      return -1;
    }
    if (std::string("received") == name) {
      result.set_received(dataset);
    }
    else if (std::string("data") == name) {
      result.set_data(dataset);
    }
    return 0;
  }

  static herr_t read_node_group(hid_t group_id, const char* name, const H5L_info2_t*, void* opdata) {
    thalamus::map<std::string, H5Node>& results = *static_cast<thalamus::map<std::string, H5Node>*>(opdata);

    H5Handle group = H5Gopen2(group_id, name, H5P_DEFAULT);
    if (!group) {
      return -1;
    }
    auto& node = results[name];
    node.name = name;
    node.set_group(group);
    auto err = H5Literate2(group, H5_INDEX_NAME, H5_ITER_INC, NULL, read_node, &node);

    return err;
  }

  ::grpc::Status Service::replay(::grpc::ServerContext*, const ::thalamus_grpc::ReplayRequest* request, ::util_grpc::Empty*) {
    return StacktraceAndAbortOnException<::grpc::Status>([&] {
      H5Handle file_handle = H5Fopen(request->filename().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
      if (!file_handle) {
        return ::grpc::Status(grpc::INTERNAL, absl::StrFormat("Failed to open file: %s", request->filename()));
      }

      H5Handle analog_group = H5Gopen2(file_handle, "analog", H5P_DEFAULT);
      if (!file_handle) {
        return ::grpc::Status(grpc::INTERNAL, absl::StrFormat("'analog' group missing from file: %s", request->filename()));
      }
      H5Handle xsens_group = H5Gopen2(file_handle, "xsens", H5P_DEFAULT);
      if (!file_handle) {
        return ::grpc::Status(grpc::INTERNAL, absl::StrFormat("'xsens' group missing from file: %s", request->filename()));
      }

      thalamus::map<std::string, H5Node> nodes;

      auto xsens_data_exists = H5Lexists(xsens_group, "data", H5P_DEFAULT);
      if (xsens_data_exists < 0) {
        return ::grpc::Status(grpc::INTERNAL, "Failed to inspect xsens group: %s");
      }
      else if (xsens_data_exists) {
        auto node_name = "xsens";
        auto& xsens_node = nodes[node_name];
        xsens_node.name = node_name;
        xsens_node.group = xsens_group;
        H5Literate2(xsens_group, H5_INDEX_NAME, H5_ITER_INC, NULL, read_node, &xsens_node);
      }
      else {
        H5Literate2(xsens_group, H5_INDEX_NAME, H5_ITER_INC, NULL, read_node_group, &nodes);
      }

      H5Literate2(analog_group, H5_INDEX_NAME, H5_ITER_INC, NULL, read_node_group, &nodes);

      std::set<std::string> keys;
      for (auto& pair : nodes) {
        keys.insert(pair.first);
      }

      std::chrono::nanoseconds current_time(std::numeric_limits<std::chrono::nanoseconds::rep>::max());
      H5Handle segment_type = createH5Segment();
      std::set<std::string> to_play(request->nodes().begin(), request->nodes().end());
      for (auto& key : keys) {
        if (!to_play.contains(key)) {
          nodes.erase(key);
        }
        else {
          auto& node = nodes[key];
          auto graph_node = impl->node_graph.get_node(node.name);
          auto locked_node = graph_node.lock();
          node.graph_node = locked_node.get();
          if (dynamic_cast<AnalogNode*>(locked_node.get())) {
            node.type = H5Node::Type::ANALOG;
          }
          else if (dynamic_cast<MotionCaptureNode*>(locked_node.get())) {
            node.type = H5Node::Type::XSENS;
          }
          else {
            return ::grpc::Status(grpc::INTERNAL, absl::StrFormat("Failed to get type of node: %s", node.name));
          }

          node.segment_type = segment_type;
          auto received_time = node.next_received();
          if (received_time) {
            current_time = std::min(*received_time, current_time);
          }
        }
      }

      thalamus::optional<std::chrono::nanoseconds> next_current_time = current_time;
      while (next_current_time) {
        std::this_thread::sleep_for(*next_current_time - current_time);
        current_time = *next_current_time;
        next_current_time = std::nullopt;
        for (auto& pair : nodes) {
          auto next_received = pair.second.next_received();
          if (next_received == current_time) {
            if (pair.second.type == H5Node::Type::ANALOG) {
              auto analog_node = dynamic_cast<AnalogNode*>(pair.second.graph_node);
              analog_node->inject(pair.second.get_data(), pair.second.sample_intervals, { "" });
              pair.second.pop_received();
            }
            else if (pair.second.type == H5Node::Type::XSENS) {
              auto xsens_node = dynamic_cast<MotionCaptureNode*>(pair.second.graph_node);
              xsens_node->inject(pair.second.get_xsens_data());
              pair.second.pop_received();
            }
            next_received = pair.second.next_received();
          }
          if (next_current_time && next_received) {
            next_current_time = std::min(*next_received, *next_current_time);
          }
          else if (next_received) {
            next_current_time = next_received;
          }
        }
      }

      return ::grpc::Status::OK;
      });
  }

  ::grpc::Status Service::eval(::grpc::ServerContext* context, ::grpc::ServerReaderWriter< ::thalamus_grpc::EvalRequest, ::thalamus_grpc::EvalResponse>* stream) {
    tracing::SetCurrentThreadName("eval");
    Impl::ContextGuard guard(this, context);
    ::thalamus_grpc::EvalResponse response;
    impl->eval_stream = stream;
    while (stream->Read(&response)) {
      TRACE_EVENT0("thalamus", "eval");
      std::unique_lock<std::mutex> lock(impl->mutex);
      auto i = impl->eval_promises.find(response.id());
      auto& promise = i->second;

      boost::json::value parsed = boost::json::parse(response.value());
      auto value = ObservableCollection::from_json(parsed);

      promise.set_value(value);
      TRACE_EVENT_ASYNC_END0("thalamus", "evaluate", response.id());
    }
    return ::grpc::Status::OK;
  }

  std::future<ObservableCollection::Value> Service::evaluate(const std::string& code) {
    TRACE_EVENT_ASYNC_BEGIN0("thalamus", "evaluate", impl->next_id);

    thalamus_grpc::EvalRequest request;
    request.set_code(code);
    request.set_id(++impl->next_id);

    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->eval_promises[request.id()] = std::promise<ObservableCollection::Value>();
    }

    auto writer = impl->eval_stream.load();
    BOOST_ASSERT_MSG(writer != nullptr, "Attempted to evaluate code with no stream");
    writer->Write(request);

    return impl->eval_promises[request.id()].get_future();
  }

  void Service::warn(const std::string& title, const std::string& message) {
    thalamus_grpc::Notification request;
    request.set_type(thalamus_grpc::Notification::Warning);
    request.set_title(title);
    request.set_message(message);

    auto writer = impl->notification_writer.load();
    BOOST_ASSERT_MSG(writer != nullptr, "Attempted to send notification with no stream");
    writer->Write(request);
  }

  bool Service::send_change(ObservableCollection::Action action, const std::string& address, ObservableCollection::Value value, std::function<void()> callback) {
    TRACE_EVENT_ASYNC_BEGIN0("thalamus", "send_change", impl->next_id);
    if (std::this_thread::get_id() == impl->observable_bridge_thread_id.load()) {
      return false;
    }
    if (impl->io_context.stopped()) {
      return true;
    }
    auto writer = impl->observable_bridge_stream.load();
    BOOST_ASSERT_MSG(writer != nullptr, "Attempted to update state with no stream");

    auto json_value = ObservableCollection::to_json(value);
    auto string_value = boost::json::serialize(json_value);
    thalamus_grpc::ObservableChange change;
    change.set_address(address);
    change.set_value(string_value);
    if (action == ObservableCollection::Action::Set) {
      change.set_action(thalamus_grpc::ObservableChange_Action_Set);
    }
    else {
      change.set_action(thalamus_grpc::ObservableChange_Action_Delete);
    }
    change.set_id(++impl->next_id);

    {
      std::unique_lock<std::mutex> lock(impl->mutex);
      impl->pending_changes[change.id()] = callback;
    }

    writer->Write(change);
    return true;
  }

  void Service::stop() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    for (auto context : impl->contexts) {
      context->TryCancel();
    }
  }

  void Service::wait() {
    auto waiting = [&] {
      return impl->observable_bridge_stream.load() == nullptr || impl->notification_writer.load() == nullptr;
    };
    while (waiting())
    {
      THALAMUS_LOG(info) << "Waiting for state service";
      std::this_thread::sleep_for(1s);
    }
    //std::cout << "State service arrived" << std::endl;
  }
}
