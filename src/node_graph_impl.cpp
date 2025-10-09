#include <algebra_node.hpp>
#include <analog_node.hpp>
#include <aruco_node.hpp>
#include <channel_picker_node.hpp>
#include <chessboard_node.hpp>
#include <distortion_node.hpp>
#include <genicam_node.hpp>
#include <hexascope_node.hpp>
#include <image_node.hpp>
#include <intan_node.hpp>
#include <log_node.hpp>
#include <lua_node.hpp>
#include <node_graph_impl.hpp>
#include <normalize_node.hpp>
#include <oculomatic_node.hpp>
#include <ophanim_node.hpp>
#include <pupil_node.hpp>
#include <remote_node.hpp>
#include <remotelog_node.hpp>
#ifndef _WIN32
#include <ros2_node.hpp>
#endif
#ifdef _WIN32
#include <brainproducts_node.hpp>
#endif
#include <run_node.hpp>
#include <run2_node.hpp>
#include <spikeglx_node.hpp>
#include <stim_printer_node.hpp>
#include <sync_node.hpp>
#include <storage2_node.hpp>
#include <task_controller_node.hpp>
#include <thalamus_config.h>
#include <thread_pool.hpp>
#include <touchscreen_node.hpp>
#include <video_node.hpp>
#include <test_pulse_node.hpp>
#include <wallclock_node.hpp>
#include <delsys_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <grpcpp/create_channel.h>
#include <thalamus.grpc.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::chrono_literals;

struct INodeFactory {
  virtual ~INodeFactory();
  virtual Node *create(ObservableDictPtr state,
                       boost::asio::io_context &io_context,
                       NodeGraph *graph) = 0;
  virtual bool prepare() = 0;
  virtual void cleanup() = 0;
  virtual std::string type_name() = 0;
};
INodeFactory::~INodeFactory() {}

template <typename T> struct NodeFactory : public INodeFactory {
  Node *create(ObservableDictPtr state, boost::asio::io_context &io_context,
               NodeGraph *graph) override {
    return new T(state, io_context, graph);
  }
  bool prepare() override {
    constexpr bool has_prepare = requires { T::prepare(); };
    if constexpr (has_prepare) {
      return T::prepare();
    } else {
      return true;
    }
  }
  void cleanup() override {
    constexpr bool has_cleanup = requires { T::cleanup(); };
    if constexpr (has_cleanup) {
      T::cleanup();
    }
  }
  std::string type_name() override { return T::type_name(); }
};

struct NodeGraphImpl::Impl {
  ObservableListPtr nodes;
  std::vector<std::shared_ptr<Node>> node_impls;
  std::vector<std::string> node_types;
  size_t num_nodes;
  boost::asio::io_context &io_context;
  std::optional<Service *> service;
  std::vector<std::pair<thalamus_grpc::NodeSelector,
                        std::function<void(std::weak_ptr<Node>)>>>
      callbacks;
  std::vector<std::pair<thalamus_grpc::NodeSelector,
                        boost::signals2::signal<void(std::weak_ptr<Node>)>>>
      signals;
  NodeGraphImpl *outer;
  thalamus::map<std::string, std::weak_ptr<grpc::Channel>> channels;
  thalamus::map<std::string, std::unique_ptr<thalamus_grpc::Thalamus::Stub>> stubs;
  std::chrono::system_clock::time_point system_time;
  std::chrono::steady_clock::time_point steady_time;
  ThreadPool thread_pool;
  thalamus_grpc::Thalamus::Stub* stub;

  std::map<std::string, INodeFactory *> node_factories;

public:
  Impl(ObservableListPtr _nodes, boost::asio::io_context &_io_context,
       NodeGraphImpl *_outer,
       std::chrono::system_clock::time_point _system_time,
       std::chrono::steady_clock::time_point _steady_time,
       thalamus_grpc::Thalamus::Stub* _stub)
      : nodes(_nodes), num_nodes(nodes->size()), io_context(_io_context),
        outer(_outer), system_time(_system_time), steady_time(_steady_time),
        thread_pool("ThreadPool"), stub(_stub) {

    node_factories = {
        {"NONE", new NodeFactory<NoneNode>()},
        {"NIDAQ", new NodeFactory<NidaqNode>()},
        {"NIDAQ_OUT", new NodeFactory<NidaqOutputNode>()},
        {"ALPHA_OMEGA", new NodeFactory<AlphaOmegaNode>()},
        {"TOGGLE", new NodeFactory<ToggleNode>()},
        {"XSENS", new NodeFactory<XsensNode>()},
        {"HAND_ENGINE", new NodeFactory<HandEngineNode>()},
        {"WAVE", new NodeFactory<WaveGeneratorNode>()},
        {"STORAGE", new NodeFactory<StorageNode>()},
        {"STORAGE2", new NodeFactory<Storage2Node>()},
        {"RUNNER", new NodeFactory<RunNode>()},
        {"RUNNER2", new NodeFactory<Run2Node>()},
        {"OPHANIM", new NodeFactory<OphanimNode>()},
        {"TASK_CONTROLLER", new NodeFactory<TaskControllerNode>()},
        {"ANALOG", new NodeFactory<AnalogNodeImpl>()},
        {"FFMPEG", new NodeFactory<FfmpegNode>()},
        {"VIDEO", new NodeFactory<VideoNode>()},
        {"OCULOMATIC", new NodeFactory<OculomaticNode>()},
        {"DISTORTION", new NodeFactory<DistortionNode>()},
        {"GENICAM", new NodeFactory<GenicamNode>()},
        {"THREAD_POOL", new NodeFactory<ThreadPoolNode>()},
        {"CHANNEL_PICKER", new NodeFactory<ChannelPickerNode>()},
        {"NORMALIZE", new NodeFactory<NormalizeNode>()},
        {"ALGEBRA", new NodeFactory<AlgebraNode>()},
        {"LUA", new NodeFactory<LuaNode>()},
#if !defined(_WIN32) && !defined(__APPLE__)
        {"ROS2", new NodeFactory<Ros2Node>()},
#endif
#ifdef _WIN32
        {"BRAINPRODUCTS", new NodeFactory<BrainProductsNode>()},
#endif
        {"REMOTE", new NodeFactory<RemoteNode>()},
        {"REMOTE_LOG", new NodeFactory<RemoteLogNode>()},
        {"CHESSBOARD", new NodeFactory<ChessBoardNode>()},
        {"PUPIL", new NodeFactory<PupilNode>()},
        {"LOG", new NodeFactory<LogNode>()},
        {"INTAN", new NodeFactory<IntanNode>()},
        {"SPIKEGLX", new NodeFactory<SpikeGlxNode>()},
        {"SYNC", new NodeFactory<SyncNode>()},
        {"TOUCH_SCREEN", new NodeFactory<TouchScreenNode>()},
        {"STIM_PRINTER", new NodeFactory<StimPrinterNode>()},
        {"TEST_PULSE_NODE", new NodeFactory<TestPulseNode>()},
        {"WALLCLOCK", new NodeFactory<WallClockNode>()},
        //{"HEXASCOPE", new NodeFactory<HexascopeNode>()},
        {"DELSYS", new NodeFactory<DelsysNode>()},
        {"ARUCO", new NodeFactory<ArucoNode>()}};

    using namespace std::placeholders;
    auto i = node_factories.begin();
    while (i != node_factories.end()) {
      if (!i->second->prepare()) {
        i = node_factories.erase(i);
      } else {
        ++i;
      }
    }
    nodes->changed.connect(std::bind(&Impl::on_nodes, this, _1, _2, _3));
  }

  ~Impl() {
    node_impls.clear();
    auto i = node_factories.begin();
    while (i != node_factories.end()) {
      i->second->cleanup();
      delete i->second;
      ++i;
    }
  }

  void clean_signals() {
    for (auto i = signals.begin(); i != signals.end();) {
      if (i->second.empty()) {
        i = signals.erase(i);
      } else {
        ++i;
      }
    }
  }

  void on_nodes(ObservableCollection::Action a,
                const ObservableCollection::Key &k,
                const ObservableCollection::Value &v) {
    using namespace std::placeholders;
    if (a == ObservableCollection::Action::Set) {
      auto index = std::get<long long>(k);
      ObservableDictPtr node = std::get<ObservableDictPtr>(v);
      node->changed.connect(
          std::bind(&Impl::on_node, this, node.get(), _1, _2, _3));

      std::string type_str = node->at("type");
      auto factory = node_factories.at(type_str);
      auto node_impl =
          std::shared_ptr<Node>(factory->create(node, io_context, outer));
      node_impls.insert(node_impls.begin() + index, node_impl);
      node_types.insert(node_types.begin() + index, type_str);
      node->recap(std::bind(&Impl::on_node, this, node.get(), _1, _2, _3));
    }
  }

  void notify(std::function<bool(const thalamus_grpc::NodeSelector &)> selector,
              std::weak_ptr<Node> node_impl) {
    for (auto i = callbacks.begin(); i != callbacks.end();) {
      if (selector(i->first)) {
        i->second(std::weak_ptr<Node>(node_impl));
        i = callbacks.erase(i);
      } else {
        ++i;
      }
    }
    for (auto i = signals.begin(); i != signals.end();) {
      if (selector(i->first)) {
        i->second(std::weak_ptr<Node>(node_impl));
        i = signals.erase(i);
      } else if (i->second.empty()) {
        i = signals.erase(i);
      } else {
        ++i;
      }
    }
  }

  void on_node(ObservableDict *node, ObservableCollection::Action a,
               const ObservableCollection::Key &k,
               const ObservableCollection::Value &v) {
    if (a == ObservableCollection::Action::Set) {
      auto node_index = 0;
      ObservableDictPtr shared_node;
      for (auto i = 0u; i < nodes->size(); ++i) {
        shared_node = nodes->at(i);
        if (node == shared_node.get()) {
          node_index = int(i);
          break;
        }
      }

      auto key_str = std::get<std::string>(k);
      if (key_str == "type") {
        auto value_str = std::get<std::string>(v);
        if (value_str != node_types.at(size_t(node_index))) {
          auto factory = node_factories.at(value_str);

          node_impls.at(size_t(node_index))
              .reset(factory->create(shared_node, io_context, outer));
          node_types.at(size_t(node_index)) = value_str;
        }

        auto node_impl = node_impls.at(size_t(node_index));
        notify([&value_str](
                   auto &selector) { return selector.type() == value_str; },
               node_impl);
      } else if (key_str == "name") {
        auto node_impl = node_impls.at(size_t(node_index));
        auto value_str = std::get<std::string>(v);
        notify([&value_str](
                   auto &selector) { return selector.name() == value_str; },
               node_impl);
      }
    }
  }
};

NodeGraphImpl::NodeGraphImpl(ObservableListPtr nodes,
                             boost::asio::io_context &io_context,
                             std::chrono::system_clock::time_point system_time,
                             std::chrono::steady_clock::time_point steady_time,
                             thalamus_grpc::Thalamus::Stub* stub)
    : impl(new Impl(nodes, io_context, this, system_time, steady_time, stub)) {
  impl->nodes->recap();
  impl->thread_pool.start();
}

NodeGraphImpl::~NodeGraphImpl() {}

std::optional<std::string>
NodeGraphImpl::get_type_name(const std::string &type) {
  auto i = impl->node_factories.find(type);
  if (i != impl->node_factories.end()) {
    return i->second->type_name();
  } else {
    return std::nullopt;
  }
}

void NodeGraphImpl::set_service(Service *service) { impl->service = service; }

Service &NodeGraphImpl::get_service() { return **impl->service; }

std::weak_ptr<Node> NodeGraphImpl::get_node(const std::string &query_name) {
  thalamus_grpc::NodeSelector selector;
  selector.set_name(query_name);
  return get_node(selector);
}

std::weak_ptr<Node>
NodeGraphImpl::get_node(const thalamus_grpc::NodeSelector &query_name) {
  std::string key;
  std::string query;
  if (!query_name.name().empty()) {
    key = "name";
    query = query_name.name();
  } else {
    key = "type";
    query = query_name.type();
  }
  for (auto i = 0u; i < impl->nodes->size(); ++i) {
    ObservableDictPtr node = impl->nodes->at(i);
    std::string value = node->at(key);
    if (query == value) {
      return std::weak_ptr<Node>(impl->node_impls.at(i));
    }
  }
  return std::weak_ptr<Node>();
}

void NodeGraphImpl::get_node(
    const std::string &query_name,
    std::function<void(std::weak_ptr<Node>)> callback) {
  thalamus_grpc::NodeSelector selector;
  selector.set_name(query_name);
  return get_node(selector, callback);
}

void NodeGraphImpl::get_node(
    const thalamus_grpc::NodeSelector &query_name,
    std::function<void(std::weak_ptr<Node>)> callback) {
  auto value = get_node(query_name);
  if (!value.lock()) {
    impl->callbacks.emplace_back(query_name, callback);
  } else {
    callback(value);
  }
}

NodeGraph::NodeConnection NodeGraphImpl::get_node_scoped(
    const std::string &name,
    std::function<void(std::weak_ptr<Node>)> callback) {
  thalamus_grpc::NodeSelector selector;
  selector.set_name(name);
  return get_node_scoped(selector, callback);
}

NodeGraph::NodeConnection NodeGraphImpl::get_node_scoped(
    const thalamus_grpc::NodeSelector &selector,
    std::function<void(std::weak_ptr<Node>)> callback) {
  auto value = get_node(selector);
  if (!value.lock()) {
    impl->signals.emplace_back(
        selector, boost::signals2::signal<void(std::weak_ptr<Node>)>());
    boost::signals2::scoped_connection connection(
        impl->signals.back().second.connect(callback));
    return connection;
  } else {
    callback(value);
    return NodeConnection();
  }
}

std::shared_ptr<grpc::Channel>
NodeGraphImpl::get_channel(const std::string &url) {
  std::vector<std::string> tokens = absl::StrSplit(url, ':');
  int port;
  bool parsed = absl::SimpleAtoi(tokens.back(), &port);
  if(!parsed) {
    return get_channel(url + ":50050");
  }

  if (!impl->channels.contains(url) || !impl->channels[url].lock()) {
    auto channel = grpc::CreateChannel(url, grpc::InsecureChannelCredentials());
    impl->channels[url] = channel;
    return channel;
  }
  return impl->channels[url].lock();
}

thalamus_grpc::Thalamus::Stub*
NodeGraphImpl::get_thalamus_stub(const std::string &url) {
  if (!impl->stubs.contains(url)) {
    auto channel = get_channel(url);
    auto stub = thalamus_grpc::Thalamus::NewStub(channel);
    impl->stubs[url] = std::move(stub);
  }
  return impl->stubs[url].get();
}

std::chrono::system_clock::time_point
NodeGraphImpl::get_system_clock_at_start() {
  return impl->system_time;
}

std::chrono::steady_clock::time_point
NodeGraphImpl::get_steady_clock_at_start() {
  return impl->steady_time;
}

ThreadPool &NodeGraphImpl::get_thread_pool() { return impl->thread_pool; }

void NodeGraphImpl::dialog(const thalamus_grpc::Dialog &dialog) {
  auto context = std::make_shared<grpc::ClientContext>();
  auto request = std::make_shared<thalamus_grpc::Dialog>(dialog);
  auto response = std::make_shared<thalamus_grpc::Empty>();

  impl->stub->async()->dialog(context.get(), request.get(), response.get(),
        [moved_context=context,moved_request=request,moved_response=response](grpc::Status s) {
          THALAMUS_LOG(info) << "Dialog complete " << s.error_message();
        });
}

void NodeGraphImpl::log(const thalamus_grpc::Text & text) {
  (*impl->service)->log_signal(text);
}
} // namespace thalamus
