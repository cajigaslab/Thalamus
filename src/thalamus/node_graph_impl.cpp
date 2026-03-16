#include <chrono>
#include <thalamus/algebra_node.hpp>
#include <thalamus/nidaq_node.hpp>
#include <thalamus/storage_node.hpp>
#include <thalamus/xsens_node.hpp>
#include <thalamus/analog_node.hpp>
#include <thalamus/aruco_node.hpp>
#include <thalamus/alpha_omega_node.hpp>
#include <thalamus/channel_picker_node.hpp>
#include <thalamus/chessboard_node.hpp>
#include <thalamus/distortion_node.hpp>
#include <thalamus/genicam_node.hpp>
#include <thalamus/hexascope_node.hpp>
#include <thalamus/image_node.hpp>
#include <thalamus/intan_node.hpp>
#include <thalamus/log_node.hpp>
#include <thalamus/lua_node.hpp>
#include <thalamus/node_graph_impl.hpp>
#include <thalamus/normalize_node.hpp>
#include <thalamus/oculomatic_node.hpp>
#include <thalamus/ophanim_node.hpp>
#include <thalamus/pupil_node.hpp>
#include <thalamus/remote_node.hpp>
#include <thalamus/remotelog_node.hpp>
#include <variant>
#ifndef _WIN32
#include <thalamus/ros2_node.hpp>
#endif
#ifdef _WIN32
#include <thalamus/brainproducts_node.hpp>
#endif
#include <thalamus/run_node.hpp>
#include <thalamus/run2_node.hpp>
#include <thalamus/spikeglx_node.hpp>
#include <thalamus/stim_printer_node.hpp>
#include <thalamus/sync_node.hpp>
#include <thalamus/storage2_node.hpp>
#include <thalamus/task_controller_node.hpp>
#include <thalamus_config.h>
#include <thalamus/thread_pool.hpp>
#include <thalamus/touchscreen_node.hpp>
#include <thalamus/video_node.hpp>
#include <thalamus/test_pulse_node.hpp>
#include <thalamus/wallclock_node.hpp>
#include <thalamus/delsys_node.hpp>
#include <thalamus/ceci_node.hpp>
#include <thalamus/frequency_node.hpp>
#include <thalamus/samplemonitor_node.hpp>
#include <thalamus/plugin.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <grpcpp/create_channel.h>
#include <thalamus.grpc.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
 
struct ThalamusState {
  int count;
  thalamus::ObservableCollection::Value value;
};

struct ThalamusIoContext {
  boost::asio::io_context& io_context;
  ThalamusIoContext(boost::asio::io_context& _io_context) : io_context(_io_context) {}
};

struct ThalamusNodeGraph {
  thalamus::NodeGraph* graph;
  ThalamusNodeGraph(thalamus::NodeGraph* _graph) : graph(_graph) {}
};

struct ThalamusStateConnection {
  boost::signals2::scoped_connection connection;
  ThalamusStateConnection(boost::signals2::connection _connection) : connection(_connection) {}
};

struct ThalamusTimer {
  boost::asio::steady_timer timer;
  ThalamusTimer(boost::asio::io_context& _io_context) : timer(_io_context) {}
};
struct ThalamusErrorCode {
  const boost::system::error_code &error;
  ThalamusErrorCode(const boost::system::error_code &_error) : error(_error) {}
};

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

struct ExtNode : public Node, public AnalogNode, public ImageNode, public MotionCaptureNode, public TextNode {
  ThalamusNode* node;
  ThalamusAPI* api;
  ThalamusNodeFactory* factory;

  ExtNode(ThalamusNode* _node, ThalamusNodeFactory* _factory) : node(_node), factory(_factory) {}
  ~ExtNode() override;

  size_t modalities() const override {
    size_t result = 0;
    result |= node->analog != nullptr ? THALAMUS_MODALITY_ANALOG : 0;
    result |= node->mocap != nullptr ? THALAMUS_MODALITY_MOCAP : 0;
    result |= node->image != nullptr ? THALAMUS_MODALITY_IMAGE : 0;
    result |= node->text != nullptr ? THALAMUS_MODALITY_TEXT : 0;
    return result;
  }

  static std::string type_name() {
    THALAMUS_ABORT("Unimplemented");
  }

  std::span<const double> data(int channel) const override {
    auto temp = node->analog->data(node, channel);
    return std::span<const double>(temp.data, temp.data+temp.size);
  }
  std::span<const short> short_data(int channel) const override {
    auto temp = node->analog->short_data(node, channel);
    return std::span<const short>(temp.data, temp.data+temp.size);
  }
  std::span<const int> int_data(int channel) const override {
    auto temp = node->analog->int_data(node, channel);
    return std::span<const int>(temp.data, temp.data+temp.size);
  }
  std::span<const uint64_t> ulong_data(int channel) const override {
    auto temp = node->analog->ulong_data(node, channel);
    return std::span<const uint64_t>(temp.data, temp.data+temp.size);
  }
  int num_channels() const override {
    return node->analog->num_channels(node);
  }
  std::chrono::nanoseconds sample_interval(int channel) const override {
    return std::chrono::nanoseconds(node->analog->sample_interval_ns(node, channel));
  }
  std::chrono::nanoseconds time() const override {
    return std::chrono::nanoseconds(node->time_ns(node));
  }
  std::chrono::nanoseconds remote_time() const override {
    THALAMUS_ABORT("Unimplemented");
  }
  std::string_view name(int channel) const override {
    auto temp = node->analog->name(node, channel);
    if(temp == nullptr) {
      auto temp2 = node->analog->name_span(node, channel);
      return std::string_view(temp2.data, temp2.data + temp2.size);
    }
    return temp;
  }
  void inject(const thalamus::vector<std::span<double const>> &,
                      const thalamus::vector<std::chrono::nanoseconds> &,
                      const thalamus::vector<std::string_view> &) override {
    THALAMUS_ABORT("Unimplemented");
  }
  bool has_analog_data() const override {
    return node->analog->has_analog_data(node);
  }
  bool is_short_data() const override {
    return node->analog->is_short_data ? node->analog->is_short_data(node) : false;
  }
  bool is_int_data() const override {
    return node->analog->is_int_data ? node->analog->is_int_data(node) : false;
  }
  bool is_ulong_data() const override {
    return node->analog->is_ulong_data ? node->analog->is_ulong_data(node) : false;
  }

  bool is_transformed() const override {
    return node->analog->is_transformed ? node->analog->is_transformed(node) : false;
  }
  double scale(int channel) const override {
    return node->analog->scale(node, channel);
  }
  double offset(int channel) const override {
    return node->analog->offset(node, channel);
  }

  Plane plane(int i) const override {
    auto temp = node->image->plane(node, i);
    return std::span<const uint8_t>(temp.data, temp.data+temp.size);
  }
  size_t num_planes() const override {
    return node->image->num_planes(node);
  }
  Format format() const override {
    auto format = node->image->format(node);
    switch(format) {
    case ThalamusImageFormat::Gray:
      return ImageNode::Format::Gray;
    case ThalamusImageFormat::RGB:
      return ImageNode::Format::RGB;
    case ThalamusImageFormat::YUV420P:
      return ImageNode::Format::YUV420P;
    case ThalamusImageFormat::YUYV422:
      return ImageNode::Format::YUYV422;
    case ThalamusImageFormat::YUVJ420P:
      return ImageNode::Format::YUVJ420P;
    }
  }
  size_t width() const override {
    return node->image->width(node);
  }
  size_t height() const override {
    return node->image->height(node);
  }
  std::chrono::nanoseconds frame_interval() const override {
    return std::chrono::nanoseconds(node->image->frame_interval_ns(node));
  }
  void inject(const thalamus_grpc::Image &) override {
    THALAMUS_ABORT("Unimplemented");
  }
  bool has_image_data() const override {
    return node->image->has_image_data(node);
  }

  std::span<MotionCaptureNode::Segment const> segments() const override {
    auto temp = node->mocap->segments(node);
    return std::span<MotionCaptureNode::Segment const>(temp.data, temp.data+temp.size);
  }
  const std::string_view pose_name() const override {
    return node->mocap->pose_name(node);
  }
  void inject(const std::span<MotionCaptureNode::Segment const> &) override {
    THALAMUS_ABORT("Unimplemented");
  }
  bool has_motion_data() const override {
    return node->mocap->has_motion_data(node);
  }

  std::string_view text() const override {
    return node->text->text(node);
  }
  bool has_text_data() const override {
    return node->text->has_text_data(node);
  }
};


ExtNode::~ExtNode() {
  factory->destroy(factory, node);
}

struct ThalamusAPIImpl {
    static std::map<ObservableCollection::Value, ThalamusState*>* cpp_to_c;
    static std::map<ThalamusState*, ObservableCollection::Value>* c_to_cpp;
    static std::map<ObservableCollection::Value, ThalamusStateConnection*>* cpp_to_c_conns;
    static std::map<ThalamusStateConnection*, ObservableCollection::Value>* c_to_cpp_conns;
    static boost::asio::io_context* io_context;

    static ThalamusState* get_state_ref(ObservableCollection::Key key) {
      if(std::holds_alternative<std::monostate>(key)) {
        return get_state_ref(ObservableCollection::Value(std::get<std::monostate>(key)));
      } else if (std::holds_alternative<int64_t>(key)) {
        return get_state_ref(ObservableCollection::Value(std::get<int64_t>(key)));
      } else if (std::holds_alternative<bool>(key)) {
        return get_state_ref(ObservableCollection::Value(std::get<bool>(key)));
      } else if (std::holds_alternative<std::string>(key)) {
        return get_state_ref(ObservableCollection::Value(std::get<std::string>(key)));
      }
      THALAMUS_ABORT("Unexpected type");
    }

    static std::string get_path(ObservableCollection::Value state) {
      if(std::holds_alternative<ObservableDictPtr>(state)) {
        return std::get<ObservableDictPtr>(state)->address();
      } else if(std::holds_alternative<ObservableListPtr>(state)) {
        return std::get<ObservableListPtr>(state)->address();
      } else {
        return "";
      }
    }

    static ThalamusState* get_state_ref(ObservableCollection::Value val) {
      auto i = cpp_to_c->find(val);
      if(i == cpp_to_c->end()) {
        auto new_ptr = new ThalamusState{1, val};
        THALAMUS_LOG(info) << "new_state " << get_path(new_ptr->value) << " " << new_ptr->count;
        (*cpp_to_c)[val] = new_ptr;
        (*c_to_cpp)[new_ptr] = val;
        return new_ptr;
      } else {
        state_inc_ref(i->second);
        return i->second;
      }
    }

    static void state_inc_ref(ThalamusState* state) {
      ++state->count;
      THALAMUS_LOG(info) << "state_inc_ref " << get_path(state->value) << " " << state->count;
    }

    static void state_dec_ref(ThalamusState* state) {
      --state->count;
      THALAMUS_LOG(info) << "state_dec_ref " << get_path(state->value) << " " << state->count;
      if(state->count == 0) {
        c_to_cpp->erase(state);
        cpp_to_c->erase(state->value);
        delete state;
      }
    }

    static char state_is_dict(ThalamusState* value) {
      return std::holds_alternative<ObservableDictPtr>(value->value) ? 1 : 0;
    }
    static char state_is_list(ThalamusState* value) {
      return std::holds_alternative<ObservableListPtr>(value->value) ? 1 : 0;
    }
    static char state_is_string(ThalamusState* value) {
      return std::holds_alternative<std::string>(value->value) ? 1 : 0;
    }
    static char state_is_int(ThalamusState* value) {
      return std::holds_alternative<int64_t>(value->value) ? 1 : 0;
    }
    static char state_is_float(ThalamusState* value) {
      return std::holds_alternative<double>(value->value) ? 1 : 0;
    }
    static char state_is_null(ThalamusState* value) {
      return std::holds_alternative<std::monostate>(value->value) ? 1 : 0;
    }
    static char state_is_bool(ThalamusState* value) {
      return std::holds_alternative<bool>(value->value) ? 1 : 0;
    }

    static const char* state_get_string(ThalamusState* state) {
      return std::get<std::string>(state->value).c_str();
    }
    static int64_t state_get_int(ThalamusState* state) {
      return std::get<int64_t>(state->value);
    }
    static double state_get_float(ThalamusState* state) {
      return std::get<double>(state->value);
    }
    static char state_get_bool(ThalamusState* state) {
      return std::get<bool>(state->value);
    }
    
    static ThalamusState* state_get_at_name(ThalamusState* state, const char* key) {
      if(std::holds_alternative<ObservableDictPtr>(state->value)) {
        auto dict = std::get<ObservableDictPtr>(state->value);
        return get_state_ref((*dict)[key]);
      }
      THALAMUS_ABORT("State does not contain dict");
    }
    static ThalamusState* state_get_at_index(ThalamusState* state, size_t key) {
      if(std::holds_alternative<ObservableDictPtr>(state->value)) {
        auto dict = std::get<ObservableDictPtr>(state->value);
        return get_state_ref((*dict)[int64_t(key)]);
      } else if (std::holds_alternative<ObservableListPtr>(state->value)) {
        auto list = std::get<ObservableListPtr>(state->value);
        return get_state_ref((*list)[key]);
      }
      THALAMUS_ABORT("State does not contain dict or list");
    }

    static ThalamusStateConnection* state_recursive_change_connect(ThalamusState* state, ThalamusStateRecursiveCallback callback, void* data) {
      auto handler = [callback,data](ObservableCollection* source,
                                                ObservableCollection::Action action,
                                                ObservableCollection::Key key,
                                                ObservableCollection::Value value) {
        ObservableDict* as_dict;
        ObservableList* as_list;
        ThalamusState* source_wrapper;
        if((as_dict = dynamic_cast<ObservableDict*>(source)) != nullptr) {
          source_wrapper = get_state_ref(as_dict->shared_from_this());
        } else if ((as_list = dynamic_cast<ObservableList*>(source)) != nullptr) {
          source_wrapper = get_state_ref(as_list->shared_from_this());
        } else {
          THALAMUS_ABORT("state is not a collection");
        }
        auto action_wrapper = action == ObservableCollection::Action::Set ? ThalamusStateAction::Set : ThalamusStateAction::Delete;
        callback(source_wrapper, action_wrapper, get_state_ref(key), get_state_ref(value), data);
      };

      boost::signals2::connection connection;
      if(std::holds_alternative<ObservableDictPtr>(state->value)) {
        connection = std::get<ObservableDictPtr>(state->value)->recursive_changed.connect(handler);
      } else {
        connection = std::get<ObservableListPtr>(state->value)->recursive_changed.connect(handler);
      }
      return new ThalamusStateConnection(connection);
    }

    static void state_recursive_change_disconnect(ThalamusStateConnection *connection) {
      delete connection;
    }
    
    static ThalamusTimer *timer_create() {
      return new ThalamusTimer(*io_context);
    }
    static void timer_destroy(ThalamusTimer *timer) {
      delete timer;
    }
    static void timer_expire_after_ns(ThalamusTimer *timer, size_t ns) {
      timer->timer.expires_after(std::chrono::nanoseconds(ns));
    }
    static void timer_async_wait(ThalamusTimer *timer,
                                 ThalamusTimerCallback callback, void *data) {
      timer->timer.async_wait([callback, data](const boost::system::error_code& error) {
        ThalamusErrorCode error_wrapper(error);
        callback(&error_wrapper, data);
      });
    }

    static int error_code_value(ThalamusErrorCode *error) {
      return error->error.value();
    }

    static void node_ready(ThalamusNode* node) {
      auto ext_node = reinterpret_cast<ExtNode*>(node->impl);
      ext_node->ready(ext_node);
    }

    static uint64_t time_ns() {
      std::chrono::nanoseconds ns = std::chrono::steady_clock::now().time_since_epoch();
      return uint64_t(ns.count());
    }

    static int error_code_operation_aborted() {
      return boost::asio::error::operation_aborted;
    }
    static void state_recap(ThalamusState* state) {
      if(std::holds_alternative<ObservableDictPtr>(state->value)) {
        std::get<ObservableDictPtr>(state->value)->recap();
      } else if(std::holds_alternative<ObservableListPtr>(state->value)) {
        std::get<ObservableListPtr>(state->value)->recap();
      } else {
        THALAMUS_ABORT("Attempt to recap a value that is neither a dict or list");
      }
    }

    template <typename T1, typename T2> static void assign_state(struct ThalamusState* state, T1 key, T2 value) {
      if(std::holds_alternative<ObservableDictPtr>(state->value)) {
        auto coll = std::get<ObservableDictPtr>(state->value);
        (*coll)[key].assign(value);
      } else if(std::holds_alternative<ObservableListPtr>(state->value)) {
        if constexpr (std::is_integral<T1>::value) {
          auto coll = std::get<ObservableDictPtr>(state->value);
          (*coll)[key].assign(value);
        } else {
          THALAMUS_ABORT("Can only index list with integer");
        }
      } else {
        THALAMUS_ABORT("Attempt to recap a value that is neither a dict or list");
      }
    }

    static void state_set_at_name_state(struct ThalamusState* state, const char* key, struct ThalamusState* value) {
      assign_state(state, key, value->value);
    }
    static void state_set_at_name_string(struct ThalamusState* state, const char* key, const char* value) {
      assign_state(state, key, value);
    }
    static void state_set_at_name_int(struct ThalamusState* state, const char* key, int64_t value) {
      assign_state(state, key, value);
    }
    static void state_set_at_name_float(struct ThalamusState* state, const char* key, double value) {
      assign_state(state, key, value);
    }
    static void state_set_at_name_null(struct ThalamusState* state, const char* key) {
      assign_state(state, key, std::monostate());
    }
    static void state_set_at_name_bool(struct ThalamusState* state, const char* key, char value) {
      assign_state(state, key, value != 0);
    }

    static void state_set_at_index_state(struct ThalamusState* state, int64_t key, struct ThalamusState* value) {
      assign_state(state, key, value->value);
    }
    static void state_set_at_index_string(struct ThalamusState* state, int64_t key, const char* value) {
      assign_state(state, key, value);
    }
    static void state_set_at_index_int(struct ThalamusState* state, int64_t key, int64_t value) {
      assign_state(state, key, value);
    }
    static void state_set_at_index_float(struct ThalamusState* state, int64_t key, double value) {
      assign_state(state, key, value);
    }
    static void state_set_at_index_null(struct ThalamusState* state, int64_t key) {
      assign_state(state, key, std::monostate());
    }
    static void state_set_at_index_bool(struct ThalamusState* state, int64_t key, char value) {
      assign_state(state, key, value != 0);
    }

    static void io_context_post(ThalamusPostCallback callback, void* data) {
      boost::asio::post(*io_context, [callback, data] {
        callback(data);
      });
    }
};

std::map<ObservableCollection::Value, ThalamusState*>* ThalamusAPIImpl::cpp_to_c = nullptr;
std::map<ThalamusState*, ObservableCollection::Value>* ThalamusAPIImpl::c_to_cpp = nullptr;
std::map<ObservableCollection::Value, ThalamusStateConnection*>* ThalamusAPIImpl::cpp_to_c_conns = nullptr;
std::map<ThalamusStateConnection*, ObservableCollection::Value>* ThalamusAPIImpl::c_to_cpp_conns = nullptr;
boost::asio::io_context* ThalamusAPIImpl::io_context = nullptr;

struct ExtNodeFactory : public INodeFactory {
  ThalamusNodeFactory* underlying;
  ThalamusIoContext io_context;
  ThalamusNodeGraph node_graph;
  ThalamusAPI* api;

  ExtNodeFactory(ThalamusNodeFactory* _underlying, boost::asio::io_context &_io_context, NodeGraph *graph, ThalamusAPI* _api)
  : underlying(_underlying), io_context(_io_context), node_graph(graph), api(_api) {}

  Node *create(ObservableDictPtr state, boost::asio::io_context &,
               NodeGraph *) override {
    auto state_wrapper = ThalamusAPIImpl::get_state_ref(state);

    auto node = underlying->create(underlying, state_wrapper, &io_context, &node_graph);

    ThalamusAPIImpl::state_dec_ref(state_wrapper);
    auto result = new ExtNode(node, underlying);
    node->impl = result;
    return result;
  }

  bool prepare() override {
    if(underlying->prepare != nullptr) {
      return underlying->prepare(underlying);
    } else {
      return true;
    }
  }
  void cleanup() override {
    if(underlying->cleanup != nullptr) {
      return underlying->cleanup(underlying);
    }
  }
  std::string type_name() override;
};

std::string ExtNodeFactory::type_name() { return underlying->type; }


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
  std::vector<SharedLibrary>& extension;

  std::map<std::string, INodeFactory *> node_factories;

  ThalamusAPIImpl thalamus_api_impl;
  ThalamusAPI thalamus_api;
  bool creating_index = -1;

public:
  Impl(ObservableListPtr _nodes, boost::asio::io_context &_io_context,
       NodeGraphImpl *_outer,
       std::chrono::system_clock::time_point _system_time,
       std::chrono::steady_clock::time_point _steady_time,
       thalamus_grpc::Thalamus::Stub* _stub,
       std::vector<SharedLibrary>& _extension)
      : nodes(_nodes), num_nodes(nodes->size()), io_context(_io_context),
        outer(_outer), system_time(_system_time), steady_time(_steady_time),
        thread_pool("ThreadPool"), stub(_stub), extension(_extension) {

    ThalamusAPIImpl::cpp_to_c = new std::map<ObservableCollection::Value, ThalamusState*>();
    ThalamusAPIImpl::c_to_cpp = new std::map<ThalamusState*, ObservableCollection::Value>();
    ThalamusAPIImpl::io_context = &io_context;
    //ThalamusAPIImpl::cpp_to_c_conns = new std::map<ObservableCollection::Value, ThalamusStateConnection*>();
    //ThalamusAPIImpl::c_to_cpp_conns = new std::map<ThalamusStateConnection*, ObservableCollection::Value>();

    thalamus_api.state_is_dict = ThalamusAPIImpl::state_is_dict;
    thalamus_api.state_is_list = ThalamusAPIImpl::state_is_list;
    thalamus_api.state_is_string = ThalamusAPIImpl::state_is_string;
    thalamus_api.state_is_int = ThalamusAPIImpl::state_is_int;
    thalamus_api.state_is_float = ThalamusAPIImpl::state_is_float;
    thalamus_api.state_is_null = ThalamusAPIImpl::state_is_null;
    thalamus_api.state_is_bool = ThalamusAPIImpl::state_is_bool;

    thalamus_api.state_get_string = ThalamusAPIImpl::state_get_string;
    thalamus_api.state_get_int = ThalamusAPIImpl::state_get_int;
    thalamus_api.state_get_float = ThalamusAPIImpl::state_get_float;
    thalamus_api.state_get_bool = ThalamusAPIImpl::state_get_bool;

    thalamus_api.state_get_at_name = ThalamusAPIImpl::state_get_at_name;
    thalamus_api.state_get_at_index = ThalamusAPIImpl::state_get_at_index;

    thalamus_api.state_dec_ref = ThalamusAPIImpl::state_dec_ref;
    thalamus_api.state_inc_ref = ThalamusAPIImpl::state_inc_ref;

    thalamus_api.state_recursive_change_connect = ThalamusAPIImpl::state_recursive_change_connect;

    thalamus_api.state_recursive_change_disconnect = ThalamusAPIImpl::state_recursive_change_disconnect;
    thalamus_api.timer_create = ThalamusAPIImpl::timer_create;
    thalamus_api.timer_destroy = ThalamusAPIImpl::timer_destroy;
    thalamus_api.timer_expire_after_ns = ThalamusAPIImpl::timer_expire_after_ns;
    thalamus_api.timer_async_wait = ThalamusAPIImpl::timer_async_wait;
    thalamus_api.error_code_value = ThalamusAPIImpl::error_code_value;
    thalamus_api.node_ready = ThalamusAPIImpl::node_ready;
    thalamus_api.time_ns = ThalamusAPIImpl::time_ns;
    thalamus_api.error_code_operation_aborted = ThalamusAPIImpl::error_code_operation_aborted;
    thalamus_api.state_recap = ThalamusAPIImpl::state_recap;

    thalamus_api.state_set_at_name_state = ThalamusAPIImpl::state_set_at_name_state;
    thalamus_api.state_set_at_name_string = ThalamusAPIImpl::state_set_at_name_string;
    thalamus_api.state_set_at_name_int = ThalamusAPIImpl::state_set_at_name_int;
    thalamus_api.state_set_at_name_float = ThalamusAPIImpl::state_set_at_name_float;
    thalamus_api.state_set_at_name_null = ThalamusAPIImpl::state_set_at_name_null;
    thalamus_api.state_set_at_name_bool = ThalamusAPIImpl::state_set_at_name_bool;

    thalamus_api.state_set_at_index_state = ThalamusAPIImpl::state_set_at_index_state;
    thalamus_api.state_set_at_index_string = ThalamusAPIImpl::state_set_at_index_string;
    thalamus_api.state_set_at_index_int = ThalamusAPIImpl::state_set_at_index_int;
    thalamus_api.state_set_at_index_float = ThalamusAPIImpl::state_set_at_index_float;
    thalamus_api.state_set_at_index_null = ThalamusAPIImpl::state_set_at_index_null;
    thalamus_api.state_set_at_index_bool = ThalamusAPIImpl::state_set_at_index_bool;
    thalamus_api.io_context_post = ThalamusAPIImpl::io_context_post;

    node_factories = {
        {"NONE", new NodeFactory<NoneNode>()},
        {"NIDAQ", new NodeFactory<NidaqNode>()},
        {"NIDAQ_OUT", new NodeFactory<NidaqOutputNode>()},
        {"ALPHA_OMEGA", new NodeFactory<AlphaOmegaNode>()},
        {"TOGGLE", new NodeFactory<ToggleNode>()},
        {"XSENS", new NodeFactory<XsensNode>()},
        {"MOCAP", new NodeFactory<MotionCaptureNodeImpl>()},
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
        {"CECI", new NodeFactory<CeciNode>()},
        //{"HEXASCOPE", new NodeFactory<HexascopeNode>()},
        {"DELSYS", new NodeFactory<DelsysNode>()},
        {"FREQUENCY", new NodeFactory<FrequencyNode>()},
        {"SAMPLE_MONITOR", new NodeFactory<SampleMonitorNode>()},
        {"ARUCO", new NodeFactory<ArucoNode>()}};

    for(auto& ext : extension) {
      //auto library_handle = LoadLibrary("C:\\Thalamus\\ext.dll");

      //auto get_node_factories = reinterpret_cast<get_node_factories_fun>(
      //    ::GetProcAddress(library_handle, "get_node_factories"));

      auto get_node_factories = ext.load<thalamus_get_node_factories>("get_node_factories");
      THALAMUS_ASSERT(get_node_factories, "get_node_factories not found in extension");
      auto factory = get_node_factories(&thalamus_api);
      while(*factory != nullptr) {
        node_factories[(*factory)->type] = new ExtNodeFactory(*factory, io_context, outer, &thalamus_api);
        ++factory;
      }
    }

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

    delete ThalamusAPIImpl::cpp_to_c;
    delete ThalamusAPIImpl::c_to_cpp;
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
      auto index = std::get<int64_t>(k);
      ObservableDictPtr node = std::get<ObservableDictPtr>(v);
      node->changed.connect(
          std::bind(&Impl::on_node, this, node.get(), _1, _2, _3));

      std::string type_str = node->at("type");
      auto factory = node_factories.at(type_str);
      
      creating_index = index;
      auto node_impl =
          std::shared_ptr<Node>(factory->create(node, io_context, outer));
      creating_index = -1;

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
      if(creating_index == node_index) {
        return;
      }

      auto key_str = std::get<std::string>(k);
      if (key_str == "type") {
        auto value_str = std::get<std::string>(v);
        if (value_str != node_types.at(size_t(node_index))) {
          auto factory = node_factories.at(value_str);

          node_types.at(size_t(node_index)) = value_str;
          creating_index = node_index;
          node_impls.at(size_t(node_index))
              .reset(factory->create(shared_node, io_context, outer));
          creating_index = -1;
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
                             thalamus_grpc::Thalamus::Stub* stub,
                             std::vector<SharedLibrary>& extension,
                             std::optional<int> thread_policy,
                             std::optional<int> thread_priority)
    : impl(new Impl(nodes, io_context, this, system_time, steady_time, stub, extension)) {
  impl->nodes->recap();
  impl->thread_pool.start(thread_policy, thread_priority);
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
