#include <cstddef>
#include <limits>
#include <thalamus/tracing.hpp>
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
#include <thalamus/serialtouchscreen_node.hpp>
#include <thalamus/joystick_node.hpp>
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
#include <thalamus/modalities_util.hpp>
#include <thalamus/node_util.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "boost/signals2/connection.hpp"
#include <grpcpp/create_channel.h>
#include <thalamus.grpc.pb.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
 
struct ThalamusState {
  int count;
  thalamus::ObservableCollection::Value value;
};

struct ThalamusStateIter {
  thalamus::ObservableCollection::Value value;
  std::variant<thalamus::ObservableCollection::VectorIteratorWrapper,
               thalamus::ObservableCollection::MapIteratorWrapper> begin;
  std::variant<thalamus::ObservableCollection::VectorIteratorWrapper,
               thalamus::ObservableCollection::MapIteratorWrapper> pos;
  std::variant<thalamus::ObservableCollection::VectorIteratorWrapper,
               thalamus::ObservableCollection::MapIteratorWrapper> end;
  bool started;
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
  const boost::system::error_code *error;
  ThalamusErrorCode() : error(nullptr) {}
  ThalamusErrorCode(const boost::system::error_code &_error) : error(&_error) {}
};

struct ThalamusSerialPort {
  boost::asio::serial_port port;
  boost::system::error_code error;
  ThalamusErrorCode error_wrapper;
};

struct ThalamusStreamBuf {
  boost::asio::streambuf buffer;
};

struct ThalamusJson {
  size_t refs;
  boost::json::value value;
};

struct ThalamusRequestHandle {
  std::function<void(const boost::json::value &)> callback;
};

struct ThalamusNodeGetConnection {
  boost::signals2::scoped_connection connection;
};

struct ThalamusNodeReadyConnection {
  boost::signals2::scoped_connection connection;
  ThalamusNode* node;
};

static std::string to_string(const ThalamusCharSpan& span) {
  return std::string(span.data, span.size);
}

static std::string_view to_string_view(const ThalamusCharSpan& span) {
  return std::string_view(span.data, span.size);
}

inline std::ostream& operator<<(std::ostream& os, const ThalamusCharSpan& span) {
  return os.write(span.data, std::streamsize(span.size));
}

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

static void string_to_span(struct ThalamusCharSpan* output, const std::string& input) {
  auto data = new char[input.size()];
  std::copy(input.begin(), input.end(), data);
  output->data = data;
  output->size = input.size();
  output->owns_data = 1;
}

struct ExtNode : public Node, public AnalogNode, public ImageNode, public MotionCaptureNode, public TextNode {
  ThalamusNode* node;
  ThalamusNodeFactory *factory;
  ThalamusAPI *api;
  std::function<void()> drop_ready;

  ExtNode(ThalamusNode *_node, ThalamusNodeFactory *_factory, ThalamusAPI *_api)
      : node(_node), factory(_factory), api(_api) {}
  ~ExtNode() override;

  void predrop(std::function<void()> on_drop_ready) override {
    THALAMUS_LOG(info) << "*ext* ExtNode::predrop";
    drop_ready = on_drop_ready;
    node->predrop(node);
  }

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
    ThalamusDoubleSpan temp;
    node->analog->data(&temp, node, channel);
    return std::span<const double>(temp.data, temp.data+temp.size);
  }
  std::span<const short> short_data(int channel) const override {
    ThalamusShortSpan temp;
    node->analog->short_data(&temp, node, channel);
    return std::span<const short>(temp.data, temp.data+temp.size);
  }
  std::span<const int> int_data(int channel) const override {
    ThalamusIntSpan temp;
    node->analog->int_data(&temp, node, channel);
    return std::span<const int>(temp.data, temp.data+temp.size);
  }
  std::span<const uint64_t> ulong_data(int channel) const override {
    ThalamusULongSpan temp;
    node->analog->ulong_data(&temp, node, channel);
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
    return 0ns;
  }
  std::string_view name(int channel) const override {
    ThalamusCharSpan temp2;
    node->analog->name(&temp2, node, channel);
    return std::string_view(temp2.data, temp2.data + temp2.size);
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
    ThalamusByteSpan temp;
    node->image->plane(&temp, node, i);
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
    ThalamusMocapSegmentSpan temp;
    node->mocap->segments(&temp, node);
    return std::span<MotionCaptureNode::Segment const>(temp.data, temp.data+temp.size);
  }
  const std::string_view pose_name() const override {
    ThalamusCharSpan temp;
    node->mocap->pose_name(&temp, node);
    return std::string_view(temp.data, temp.data+temp.size);
  }
  void inject(const std::span<MotionCaptureNode::Segment const> &) override {
    THALAMUS_ABORT("Unimplemented");
  }
  bool has_motion_data() const override {
    return node->mocap->has_motion_data(node);
  }

  std::string_view text() const override {
    ThalamusCharSpan span;
    node->text->text(&span, node);
    return to_string_view(span);
  }

  bool has_text_data() const override {
    return node->text->has_text_data(node);
  }

  void process(const boost::json::value & request, std::function<void(const boost::json::value &)> callback) override {
    if(node->process == nullptr) {
      callback(boost::json::value());
      return;
    }
    auto json = new ThalamusJson{1, request};
    auto handle = new ThalamusRequestHandle {callback};
    node->process(node, handle, json);
    api->json_dec_ref(json);
  }
};

ExtNode::~ExtNode() {
  factory->destroy(factory, node);
}

struct Interfaces {
  Node* node = nullptr;
  AnalogNode* analog = nullptr;
  ImageNode* image = nullptr;
  MotionCaptureNode* mocap = nullptr;
  bool safe = false;
  int count = 0;
};

#define ASSERT_SAFE() do { if(!interfaces->safe) [[unlikely]] { THALAMUS_ABORT("Node should only be accessed in get_node or ready callback"); } } while(0)

static uint64_t plugin_analog_time_ns(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  std::chrono::nanoseconds t = interfaces->analog->time();
  return uint64_t(t.count());
}

static uint64_t plugin_image_time_ns(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  std::chrono::nanoseconds t = interfaces->image->time();
  return uint64_t(t.count());
}

static uint64_t plugin_mocap_time_ns(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  std::chrono::nanoseconds t = interfaces->mocap->time();
  return uint64_t(t.count());
}

static void plugin_node_process(struct ThalamusNode* node, struct ThalamusRequestHandle* handle, struct ThalamusJson* request) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  interfaces->node->process(request->value, [handle](const boost::json::value & response) {
    handle->callback(response);
    delete handle;
  });
}

static void plugin_analog_data(struct ThalamusDoubleSpan* output, struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  auto span = interfaces->analog->data(channel);
  output->data = span.data();
  output->size = span.size();
}

static void plugin_analog_short_data(struct ThalamusShortSpan* output, struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  auto span = interfaces->analog->short_data(channel);
  output->data = span.data();
  output->size = span.size();
}

static void plugin_analog_int_data(struct ThalamusIntSpan* output, struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  auto span = interfaces->analog->int_data(channel);
  output->data = span.data();
  output->size = span.size();
}

static void plugin_analog_ulong_data(struct ThalamusULongSpan* output, struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  auto span = interfaces->analog->ulong_data(channel);
  output->data = span.data();
  output->size = span.size();
}

static int plugin_analog_num_channels(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->analog->num_channels();
}

static uint64_t plugin_analog_sample_interval_ns(struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  std::chrono::nanoseconds ns = interfaces->analog->sample_interval(channel);
  return uint64_t(ns.count());
}

static void plugin_analog_name(ThalamusCharSpan* output, struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  auto name = interfaces->analog->name(channel);
  output->data = name.data();
  output->size = name.size();
}

static char plugin_analog_has_analog_data(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->analog->has_analog_data() ? 1 : 0;
}

static char plugin_analog_is_short_data(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->analog->is_short_data() ? 1 : 0;
}

static char plugin_analog_is_int_data(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->analog->is_int_data() ? 1 : 0;
}

static char plugin_analog_is_ulong_data(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->analog->is_ulong_data() ? 1 : 0;
}

static char plugin_analog_is_transformed(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->analog->is_transformed() ? 1 : 0;
}

static double plugin_analog_scale(struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->analog->scale(channel);
}

static double plugin_analog_offset(struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->analog->offset(channel);
}

static void plugin_image_plane(struct ThalamusByteSpan* output, struct ThalamusNode* node, int channel) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  auto span = interfaces->image->plane(channel);
  output->data = span.data();
  output->size = span.size();
}

static uint64_t plugin_image_num_planes(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->image->num_planes();
}

static ThalamusImageFormat plugin_image_format(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  switch(interfaces->image->format()) {
  case ImageNode::Format::Gray:
    return ThalamusImageFormat::Gray;
  case ImageNode::Format::RGB:
    return ThalamusImageFormat::RGB;
  case ImageNode::Format::YUYV422:
    return ThalamusImageFormat::YUYV422;
  case ImageNode::Format::YUV420P:
    return ThalamusImageFormat::YUV420P;
  case ImageNode::Format::YUVJ420P:
    return ThalamusImageFormat::YUVJ420P;
  }
}

static uint64_t plugin_image_width(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->image->width();
}

static uint64_t plugin_image_height(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->image->height();
}

static uint64_t plugin_image_frame_interval_ns(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  std::chrono::nanoseconds ns = interfaces->image->frame_interval();
  return uint64_t(ns.count());
}

static char plugin_image_has_image_data(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->image->has_image_data() ? 1 : 0;
}

static void plugin_mocap_segments(ThalamusMocapSegmentSpan* output, struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  auto segments = interfaces->mocap->segments();
  output->data = segments.data();
  output->size = segments.size();
}

static void plugin_mocap_pose_name(ThalamusCharSpan* output, struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  auto name = interfaces->mocap->pose_name();
  output->data = name.data();
  output->size = name.size();
}

static char plugin_mocap_has_motion_data(struct ThalamusNode* node) {
  auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
  ASSERT_SAFE();
  return interfaces->mocap->has_motion_data() ? 1 : 0;
}

struct NodeGuard {
  Interfaces* interfaces;
  NodeGuard(ThalamusNode* node) : interfaces(reinterpret_cast<Interfaces*>(node->impl)) {
    interfaces->safe = true;
  }
  ~NodeGuard() {
    interfaces->safe = false;
  }
};

struct ThalamusAPIImpl {
  static std::map<ObservableCollection::Value, ThalamusState*>* cpp_to_c;
  static std::map<ThalamusState*, ObservableCollection::Value>* c_to_cpp;

  static std::map<Node*, ThalamusNode*>* node_cpp_to_c;
  static std::map<ThalamusNode*, Node*>* node_c_to_cpp;

  static boost::asio::io_context* io_context;
  static NodeGraphImpl* node_graph;

  static ThalamusNode* wrap_node(Node* node) {
    auto result = new ThalamusNode();
    memset(result, 0, sizeof(ThalamusNode));

    auto interfaces = new Interfaces();
    interfaces->node = node;
    interfaces->count = 1;
    result->impl = interfaces;
    result->process = plugin_node_process;

    auto analog = node_cast<AnalogNode *>(node);
    auto image = node_cast<ImageNode *>(node);
    auto mocap = node_cast<MotionCaptureNode *>(node);
    if(analog) {
      interfaces->analog = analog;
      result->time_ns = result->time_ns ? result->time_ns : plugin_analog_time_ns;
      result->analog = new ThalamusAnalogNode();
      result->analog->data = plugin_analog_data;
      result->analog->short_data = plugin_analog_short_data;
      result->analog->int_data = plugin_analog_int_data;
      result->analog->ulong_data = plugin_analog_ulong_data;
      result->analog->num_channels = plugin_analog_num_channels;
      result->analog->sample_interval_ns = plugin_analog_sample_interval_ns;
      result->analog->name = plugin_analog_name;
      result->analog->has_analog_data = plugin_analog_has_analog_data;
      result->analog->is_short_data = plugin_analog_is_short_data;
      result->analog->is_int_data = plugin_analog_is_int_data;
      result->analog->is_ulong_data = plugin_analog_is_ulong_data;
      result->analog->is_transformed = plugin_analog_is_transformed;
      result->analog->scale = plugin_analog_scale;
      result->analog->offset = plugin_analog_offset;
    }
    if (image) {
      interfaces->image = image;
      result->time_ns = result->time_ns ? result->time_ns : plugin_image_time_ns;
      result->image = new ThalamusImageNode();
      result->image->plane = plugin_image_plane;
      result->image->num_planes = plugin_image_num_planes;
      result->image->format = plugin_image_format;
      result->image->width = plugin_image_width;
      result->image->height = plugin_image_height;
      result->image->frame_interval_ns = plugin_image_frame_interval_ns;
      result->image->has_image_data = plugin_image_has_image_data;
    }
    if (mocap) {
      interfaces->mocap = mocap;
      result->time_ns = result->time_ns ? result->time_ns : plugin_mocap_time_ns;
      result->mocap = new ThalamusMocapNode();
      result->mocap->segments = plugin_mocap_segments;
      result->mocap->pose_name = plugin_mocap_pose_name;
      result->mocap->has_motion_data = plugin_mocap_has_motion_data;
    }

    return result;
  }

  static ThalamusNode* get_node_ref(Node* val) {
    auto i = node_cpp_to_c->find(val);
    if(i == node_cpp_to_c->end()) {
      auto new_ptr = wrap_node(val);
      //THALAMUS_LOG(info) << "new_state " << get_path(new_ptr->value) << " " << new_ptr->count;
      (*node_cpp_to_c)[val] = new_ptr;
      (*node_c_to_cpp)[new_ptr] = val;
      return new_ptr;
    } else {
      node_inc_ref(i->second);
      return i->second;
    }
  }

  static ObservableCollection::Value key_to_value(ObservableCollection::Key key) {
    if(std::holds_alternative<std::monostate>(key)) {
      return ObservableCollection::Value(std::get<std::monostate>(key));
    } else if (std::holds_alternative<int64_t>(key)) {
      return ObservableCollection::Value(std::get<int64_t>(key));
    } else if (std::holds_alternative<bool>(key)) {
      return ObservableCollection::Value(std::get<bool>(key));
    } else if (std::holds_alternative<std::string>(key)) {
      return ObservableCollection::Value(std::get<std::string>(key));
    }
    THALAMUS_ABORT("Unexpected type");
  }

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
      THALAMUS_LOG(trace) << "new_state " << get_path(new_ptr->value) << " " << new_ptr->count;
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
    THALAMUS_LOG(trace) << "state_inc_ref " << get_path(state->value) << " " << state->count;
  }

  static void state_dec_ref(ThalamusState* state) {
    --state->count;
    THALAMUS_LOG(trace) << "state_dec_ref " << get_path(state->value) << " " << state->count;
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

  static void state_get_string(ThalamusCharSpan* span, ThalamusState* state) {
    const auto& s = std::get<std::string>(state->value);
    span->data = s.data();
    span->size = s.size();
    span->owns_data = 0;
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
  
  static ThalamusState* state_get_at_name(ThalamusState* state, const ThalamusCharSpan* key) {
    std::string key_str(key->data, key->size);
    if(std::holds_alternative<ObservableDictPtr>(state->value)) {
      auto dict = std::get<ObservableDictPtr>(state->value);
      if(dict->contains(key_str)) {
        return get_state_ref((*dict)[key_str]);
      } else {
        return nullptr;
      }
    }
    THALAMUS_ABORT("State does not contain dict");
  }
  static ThalamusState* state_get_at_index(ThalamusState* state, uint64_t key) {
    if(std::holds_alternative<ObservableDictPtr>(state->value)) {
      auto dict = std::get<ObservableDictPtr>(state->value);
      if(dict->contains(int64_t(key))) {
        return get_state_ref((*dict)[int64_t(key)]);
      } else {
        return nullptr;
      }
    } else if (std::holds_alternative<ObservableListPtr>(state->value)) {
      auto list = std::get<ObservableListPtr>(state->value);
      if(key < list->size()) {
        return get_state_ref((*list)[key]);
      } else {
        return nullptr;
      }
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
      auto wrapped_key = get_state_ref(key);
      auto wrapped_value = get_state_ref(value);
      callback(source_wrapper, action_wrapper, wrapped_key, wrapped_value, data);
      state_dec_ref(source_wrapper);
      state_dec_ref(wrapped_key);
      state_dec_ref(wrapped_value);
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
  static void timer_expire_after_ns(ThalamusTimer *timer, uint64_t ns) {
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
    return error->error->value();
  }

  static void node_ready(ThalamusNode* node) {
    auto ext_node = reinterpret_cast<ExtNode*>(node->impl);
    ext_node->ready(ext_node);
  }

  static void node_ready_offmain(ThalamusNode* node) {
    auto ext_node = reinterpret_cast<ExtNode*>(node->impl);
    node::signal_ready_offmain(ext_node, *io_context);
  }

  static void node_predrop_ready(ThalamusNode* node) {
    THALAMUS_LOG(info) << "*ext* node_predrop_ready";
    auto ext_node = reinterpret_cast<ExtNode*>(node->impl);
    THALAMUS_ASSERT(ext_node->drop_ready, "No predrop callback available");
    ext_node->drop_ready();
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

  static void state_recap_with(ThalamusState* state, ThalamusStateRecursiveCallback callback, void* data) {
    auto handler = [state,callback,data](ObservableCollection::Action action,
                                   ObservableCollection::Key key,
                                   ObservableCollection::Value value) {
      auto action_wrapper = action == ObservableCollection::Action::Set ? ThalamusStateAction::Set : ThalamusStateAction::Delete;
      auto wrapped_key = get_state_ref(key);
      auto wrapped_value = get_state_ref(value);
      callback(state, action_wrapper, wrapped_key, wrapped_value, data);
      state_dec_ref(wrapped_key);
      state_dec_ref(wrapped_value);
    };

    if(std::holds_alternative<ObservableDictPtr>(state->value)) {
      std::get<ObservableDictPtr>(state->value)->recap(handler);
    } else if(std::holds_alternative<ObservableListPtr>(state->value)) {
      std::get<ObservableListPtr>(state->value)->recap(handler);
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
        auto coll = std::get<ObservableListPtr>(state->value);
        if(size_t(key) < coll->size()) {
          (*coll)[size_t(key)].assign(value);
        } else {
          THALAMUS_ABORT("Index out of bounds");
        }
      } else {
        THALAMUS_ABORT("Can only index list with integer");
      }
    } else {
      THALAMUS_ABORT("Attempt to recap a value that is neither a dict or list");
    }
  }

  static void state_set_at_name_state(struct ThalamusState* state, const ThalamusCharSpan* key, struct ThalamusState* value) {
    assign_state(state, std::string(key->data, key->size), value->value);
  }
  static void state_set_at_name_string(struct ThalamusState* state, const ThalamusCharSpan* key, const ThalamusCharSpan* value) {
    assign_state(state, std::string(key->data, key->size), std::string(value->data, value->size));
  }
  static void state_set_at_name_int(struct ThalamusState* state, const ThalamusCharSpan* key, int64_t value) {
    assign_state(state, std::string(key->data, key->size), value);
  }
  static void state_set_at_name_float(struct ThalamusState* state, const ThalamusCharSpan* key, double value) {
    assign_state(state, std::string(key->data, key->size), value);
  }
  static void state_set_at_name_null(struct ThalamusState* state, const ThalamusCharSpan* key) {
    assign_state(state, std::string(key->data, key->size), std::monostate());
  }
  static void state_set_at_name_bool(struct ThalamusState* state, const ThalamusCharSpan* key, char value) {
    assign_state(state, std::string(key->data, key->size), value != 0);
  }

  static void state_set_at_index_state(struct ThalamusState* state, int64_t key, struct ThalamusState* value) {
    assign_state(state, key, value->value);
  }
  static void state_set_at_index_string(struct ThalamusState* state, int64_t key, const ThalamusCharSpan* value) {
    assign_state(state, key, std::string(value->data, value->size));
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

  static struct ThalamusState* state_parent(struct ThalamusState* state) {
    ObservableCollection* parent = nullptr;
    if(std::holds_alternative<ObservableDictPtr>(state->value)) {
      parent = std::get<ObservableDictPtr>(state->value)->parent;
    } else if (std::holds_alternative<ObservableListPtr>(state->value)) {
      parent = std::get<ObservableListPtr>(state->value)->parent;
    } else {
      THALAMUS_ABORT("Attempt to recap a value that is neither a dict or list");
    }
    if (parent == nullptr) {
      return nullptr;
    }

    ObservableList* list;
    ObservableDict* dict;
    ObservableCollection::Value parent_value;
    if((list = parent->as_list())) {
      parent_value = ObservableCollection::Value(list->shared_from_this());
    } else if ((dict = parent->as_dict())) {
      parent_value = ObservableCollection::Value(dict->shared_from_this());
    } else {
      return nullptr;
    }
    return get_state_ref(parent_value);
  }

  static struct ThalamusStateIter* state_iter_create(struct ThalamusState* state) {
    if(std::holds_alternative<ObservableDictPtr>(state->value)) {
      auto value = std::get<ObservableDictPtr>(state->value);
      return new ThalamusStateIter{state->value, value->begin(), value->begin(), value->end(), false};
    } else if (std::holds_alternative<ObservableListPtr>(state->value)) {
      auto value = std::get<ObservableListPtr>(state->value);
      return new ThalamusStateIter{state->value, value->begin(), value->begin(), value->end(), false};
    }
    THALAMUS_ABORT("Attempt to iterate a value that is neither a dict or list");
  }

  static uint8_t state_iter_next(struct ThalamusStateIter* iter) {
    THALAMUS_ASSERT(iter->pos != iter->end, "Attempted to advance finished iterator");
    if(std::holds_alternative<thalamus::ObservableCollection::VectorIteratorWrapper>(iter->begin)) {
      auto& pos = std::get<thalamus::ObservableCollection::VectorIteratorWrapper>(iter->pos);
      auto end = std::get<thalamus::ObservableCollection::VectorIteratorWrapper>(iter->pos);
      if(iter->started) {
        ++pos;
      } else {
        iter->started = true;
      }
      return iter->pos == iter->end ? 0 : 1;
    } else if (std::holds_alternative<thalamus::ObservableCollection::MapIteratorWrapper>(iter->begin)) {
      auto& pos = std::get<thalamus::ObservableCollection::MapIteratorWrapper>(iter->pos);
      auto end = std::get<thalamus::ObservableCollection::MapIteratorWrapper>(iter->pos);
      if(iter->started) {
        ++pos;
      } else {
        iter->started = true;
      }
      return iter->pos == iter->end ? 0 : 1;
    }
    THALAMUS_ABORT("Bad Iterator");
  }

  static struct ThalamusState* state_iter_key(struct ThalamusStateIter* iter) {
    THALAMUS_ASSERT(iter->pos != iter->end, "Attempted to read finished iterator");
    thalamus::ObservableCollection::Value result;
    if(std::holds_alternative<thalamus::ObservableCollection::VectorIteratorWrapper>(iter->begin)) {
      auto begin = std::get<thalamus::ObservableCollection::VectorIteratorWrapper>(iter->begin);
      auto pos = std::get<thalamus::ObservableCollection::VectorIteratorWrapper>(iter->pos);
      result = int64_t(std::distance(begin, pos));
    } else if (std::holds_alternative<thalamus::ObservableCollection::MapIteratorWrapper>(iter->begin)) {
      auto pos = std::get<thalamus::ObservableCollection::MapIteratorWrapper>(iter->pos);
      result = key_to_value(pos->first);
    }
    return get_state_ref(result);
  }

  static struct ThalamusState* state_iter_value(struct ThalamusStateIter* iter) {
    THALAMUS_ASSERT(iter->pos != iter->end, "Attempted to read finished iterator");
    thalamus::ObservableCollection::Value result;
    if(std::holds_alternative<thalamus::ObservableCollection::VectorIteratorWrapper>(iter->begin)) {
      auto pos = std::get<thalamus::ObservableCollection::VectorIteratorWrapper>(iter->pos);
      result = *pos;
    } else if (std::holds_alternative<thalamus::ObservableCollection::MapIteratorWrapper>(iter->begin)) {
      auto pos = std::get<thalamus::ObservableCollection::MapIteratorWrapper>(iter->pos);
      result = pos->second;
    }
    return get_state_ref(result);
  }

  static struct ThalamusState* state_key_of(struct ThalamusState* parent, struct ThalamusState* child) {
    ObservableCollection* parent_col = nullptr;
    ObservableCollection* child_col = nullptr;
    if(std::holds_alternative<ObservableDictPtr>(parent->value)) {
      parent_col = std::get<ObservableDictPtr>(parent->value).get();
    } else if (std::holds_alternative<ObservableListPtr>(parent->value)) {
      parent_col = std::get<ObservableListPtr>(parent->value).get();
    } else {
      THALAMUS_ABORT("Attempt to iterate a value that is neither a dict or list");
    }
    if(std::holds_alternative<ObservableDictPtr>(child->value)) {
      child_col = std::get<ObservableDictPtr>(child->value).get();
    } else if (std::holds_alternative<ObservableListPtr>(child->value)) {
      child_col = std::get<ObservableListPtr>(child->value).get();
    } else {
      THALAMUS_ABORT("Attempt to iterate a value that is neither a dict or list");
    }

    auto key = parent_col->key_of(*child_col);

    return key ? get_state_ref(*key) : nullptr;
  }

  static void state_iter_destroy(struct ThalamusStateIter* iter) {
    delete iter;
  }

  static void io_context_post(ThalamusPostCallback callback, void* data) {
    boost::asio::post(*io_context, [callback, data] {
      callback(data);
    });
  }

  static void threadpool_post(ThalamusPostCallback callback, void* data) {
    node_graph->get_thread_pool().push([callback, data] {
      callback(data);
    });
  }

  static void trace_event_begin(const ThalamusCharSpan* name) {
    TRACE_EVENT_BEGIN("plugin", perfetto::DynamicString(name->data, name->size));
  }

  static void trace_event_begin_span(const ThalamusCharSpan* name) {
    TRACE_EVENT_BEGIN("plugin", perfetto::DynamicString(name->data, name->size));
  }

  static void trace_event_end() {
    TRACE_EVENT_END("plugin");
  }

  static ThalamusSerialPort* serial_port_create() {
    auto result = new ThalamusSerialPort{
      boost::asio::serial_port(*io_context),
      boost::system::error_code(),
      ThalamusErrorCode()
    };
    result->error_wrapper.error = &result->error;
    return result;
  }

  static void serial_port_destroy(ThalamusSerialPort* port) {
    delete port;
  }

  static void serial_set_baud_rate(ThalamusSerialPort* port, uint32_t rate) {
    port->port.set_option(boost::asio::serial_port_base::baud_rate(rate));
  }

  static void serial_port_open(ThalamusSerialPort* port, const ThalamusCharSpan* name) {
    port->port.open(std::string(name->data, name->size), port->error);
  }

  static ThalamusErrorCode* serial_port_error(ThalamusSerialPort* port) {
    return &port->error_wrapper;
  }

  static void serial_port_read_until(ThalamusSerialPort* port, ThalamusStreamBuf* buffer, const ThalamusCharSpan* delimiter, ThalamusIOCallback callback, void* data) {
    auto view = boost::asio::string_view(delimiter->data, delimiter->size);
    boost::asio::async_read_until(port->port, buffer->buffer, view, [callback, data] (auto ec, auto count) {
      ThalamusErrorCode err{ec};
      callback(&err, count, data);
    });
  }

  static void serial_port_read_some(ThalamusSerialPort* port, ThalamusMutableByteSpan* span, ThalamusIOCallback callback, void* data) {
    port->port.async_read_some(boost::asio::buffer(span->data, span->size), [callback, data] (auto ec, auto count) {
      ThalamusErrorCode err{ec};
      callback(&err, count, data);
    });
  }

  static void serial_port_read(ThalamusSerialPort* port, ThalamusMutableByteSpan* span, ThalamusIOCallback callback, void* data) {
    boost::asio::async_read(port->port, boost::asio::buffer(span->data, span->size), [callback, data] (auto ec, auto count) {
      ThalamusErrorCode err{ec};
      callback(&err, count, data);
    });
  }

  static void serial_port_write(ThalamusSerialPort* port, ThalamusByteSpan* span, ThalamusIOCallback callback, void* data) {
    boost::asio::async_write(port->port, boost::asio::buffer(span->data, span->size), [callback, data] (auto ec, auto count) {
      ThalamusErrorCode err{ec};
      callback(&err, count, data);
    });
  }

  static ThalamusStreamBuf* streambuf_create() {
    auto result = new ThalamusStreamBuf{
      boost::asio::streambuf()
    };
    return result;
  }

  static void streambuf_destroy(ThalamusStreamBuf* port) {
    delete port;
  }

  static void streambuf_to_span(struct ThalamusCharSpan* result, ThalamusStreamBuf* buffer) {
    std::istream stream(&buffer->buffer);
    auto size = buffer->buffer.size();
    char* data = new char[size];
    std::copy_n(std::istreambuf_iterator<char>(&buffer->buffer), buffer->buffer.size(), data);

    result->data = data;
    result->size = size;
    result->owns_data = true;
  }

  static void streambuf_consume(ThalamusStreamBuf* buffer, uint64_t count) {
    buffer->buffer.consume(count);
  }

  static uint64_t streambuf_size(ThalamusStreamBuf* buffer) {
    return buffer->buffer.size();
  }

  static void charspan_release(ThalamusCharSpan* span) {
    if(span->owns_data) {
      delete span->data;
    }
  }

  static void error_code_message(struct ThalamusCharSpan* result, ThalamusErrorCode *error) {
    auto message = error->error->message();
    char* data = new char[message.size()];
    std::copy_n(message.begin(), message.size(), data);
    result->data = data;
    result->size = message.size();
    result->owns_data = true;
  }

  static void json_to_string(struct ThalamusCharSpan* output, const struct ThalamusJson* input) {
    auto serialized = boost::json::serialize(input->value);
    string_to_span(output, serialized);
  }

  static struct ThalamusJson* json_from_string(const struct ThalamusCharSpan* input) {
    return new ThalamusJson{1, boost::json::parse(std::string_view(input->data, input->size))};
  }

  static void json_inc_ref(struct ThalamusJson* input) {
    ++input->refs;
  }

  static void json_dec_ref(struct ThalamusJson* input) {
    if(--input->refs == 0) {
      delete input;
    }
  }

  static void request_respond(struct ThalamusRequestHandle* handle, const struct ThalamusJson* response) {
    handle->callback(response->value);
    delete handle;
  }

  static ThalamusNodeGetConnection* node_get_node(struct ThalamusNodeSelector* c_selector, ThalamusNodeGetCallback callback, void* data) {
    thalamus_grpc::NodeSelector selector;
    if(c_selector->name.data) {
      selector.set_name(std::string(c_selector->name.data, c_selector->name.size));
    }
    if(c_selector->type.data) {
      selector.set_type(std::string(c_selector->type.data, c_selector->type.size));
    }

    auto result = new ThalamusNodeGetConnection();
    result->connection = node_graph->get_node_scoped(selector, [callback, data] (auto node) {
      auto ref = get_node_ref(node.lock().get());
      {
        NodeGuard lock(ref);
        callback(ref, data);
      }
      node_dec_ref(ref);
    });
    return result;
  }

  static void node_inc_ref(struct ThalamusNode* node) {
    auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
    ++interfaces->count;
  }

  static void node_dec_ref(struct ThalamusNode* node) {
    auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
    if(--interfaces->count == 0) {
      node_c_to_cpp->erase(node);
      node_cpp_to_c->erase(interfaces->node);
      delete node;
    }
  }

  static ThalamusNodeReadyConnection* node_ready_connect(struct ThalamusNode* node, ThalamusNodeReadyCallback callback, void* data) {
    auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
    
    auto result = new ThalamusNodeReadyConnection();
    result->node = node;
    node_inc_ref(node);
    result->connection = interfaces->node->ready.connect([node, callback, data] (auto) {
      NodeGuard lock(node);
      callback(node, data);
    });
    return result;
  }

  static ThalamusNodeReadyConnection* node_ready_multithreaded_connect(struct ThalamusNode* node, ThalamusNodeReadyCallback callback, void* data) {
    auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
    
    auto result = new ThalamusNodeReadyConnection();
    result->node = node;
    node_inc_ref(node);
    result->connection = node::connect_ready_multithreaded(interfaces->node, [node, callback, data] (auto) {
      NodeGuard lock(node);
      callback(node, data);
    });
    return result;
  }

  static void node_get_node_disconnect(struct ThalamusNodeGetConnection* conn) {
    delete conn;
  }
  static void node_ready_disconnect(struct ThalamusNodeReadyConnection* conn) {
    node_dec_ref(conn->node);
    delete conn;
  }

  static void node_channels_changed(struct ThalamusNode* node) {
    auto ext_node = reinterpret_cast<ExtNode*>(node->impl);
    ext_node->channels_changed(ext_node);
  }

  static ThalamusNodeReadyConnection* node_channels_changed_connect(struct ThalamusNode* node, ThalamusNodeReadyCallback callback, void* data) {
    auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
    
    auto result = new ThalamusNodeReadyConnection();
    result->node = node;
    node_inc_ref(node);
    result->connection = interfaces->analog->channels_changed.connect([node, callback, data] (auto) {
      NodeGuard lock(node);
      callback(node, data);
    });
    return result;
  }

  static void node_channels_changed_disconnect(struct ThalamusNodeReadyConnection* conn) {
    node_dec_ref(conn->node);
    delete conn;
  }

  static ThalamusState* node_get_state(struct ThalamusNode* node) {
    auto interfaces = reinterpret_cast<Interfaces*>(node->impl);
    ASSERT_SAFE();
    return get_state_ref(node_graph->get_node_state(interfaces->node));
  }
};

std::map<ObservableCollection::Value, ThalamusState*>* ThalamusAPIImpl::cpp_to_c = nullptr;
std::map<ThalamusState*, ObservableCollection::Value>* ThalamusAPIImpl::c_to_cpp = nullptr;
boost::asio::io_context* ThalamusAPIImpl::io_context = nullptr;
NodeGraphImpl* ThalamusAPIImpl::node_graph = nullptr;


std::map<Node*, ThalamusNode*>* ThalamusAPIImpl::node_cpp_to_c = nullptr;
std::map<ThalamusNode*, Node*>* ThalamusAPIImpl::node_c_to_cpp = nullptr;

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
    auto result = new ExtNode(node, underlying, api);
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

std::string ExtNodeFactory::type_name() { return std::string("*EXT* ") + to_string(underlying->type); }


struct NodeGraphImpl::Impl {
  ObservableListPtr nodes;
  std::vector<std::string> node_next_type;
  std::vector<std::shared_ptr<Node>> node_impls;
  std::vector<std::string> node_types;
  std::vector<ObservableDictPtr> node_configs;
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
  Vulkan vulkan;

  std::map<std::string, INodeFactory *> node_factories;

  ThalamusAPIImpl thalamus_api_impl;
  ThalamusAPI thalamus_api;
  int creating_index = -1;
  boost::signals2::scoped_connection nodes_connection;
  std::vector<boost::signals2::scoped_connection> node_connections;

public:
  Impl(ObservableListPtr _nodes, boost::asio::io_context &_io_context,
       NodeGraphImpl *_outer,
       std::chrono::system_clock::time_point _system_time,
       std::chrono::steady_clock::time_point _steady_time,
       thalamus_grpc::Thalamus::Stub* _stub,
       std::vector<SharedLibrary>& _extension, Vulkan _vulkan)
      : nodes(_nodes), num_nodes(nodes->size()), io_context(_io_context),
        outer(_outer), system_time(_system_time), steady_time(_steady_time),
        thread_pool("ThreadPool"), stub(_stub), extension(_extension), vulkan(_vulkan) {

    ThalamusAPIImpl::cpp_to_c = new std::map<ObservableCollection::Value, ThalamusState*>();
    ThalamusAPIImpl::c_to_cpp = new std::map<ThalamusState*, ObservableCollection::Value>();
    ThalamusAPIImpl::node_cpp_to_c = new std::map<Node*, ThalamusNode*>();
    ThalamusAPIImpl::node_c_to_cpp = new std::map<ThalamusNode*, Node*>();
    ThalamusAPIImpl::io_context = &io_context;
    ThalamusAPIImpl::node_graph = _outer;

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
    thalamus_api.trace_event_begin = ThalamusAPIImpl::trace_event_begin;
    thalamus_api.trace_event_begin_span = ThalamusAPIImpl::trace_event_begin_span;
    thalamus_api.trace_event_end = ThalamusAPIImpl::trace_event_end;
    thalamus_api.serial_port_create = ThalamusAPIImpl::serial_port_create;
    thalamus_api.serial_port_destroy = ThalamusAPIImpl::serial_port_destroy;
    thalamus_api.serial_set_baud_rate = ThalamusAPIImpl::serial_set_baud_rate;
    thalamus_api.serial_port_open = ThalamusAPIImpl::serial_port_open;
    thalamus_api.serial_port_error = ThalamusAPIImpl::serial_port_error;
    
    thalamus_api.serial_port_read_until = ThalamusAPIImpl::serial_port_read_until;
    thalamus_api.serial_port_read_some = ThalamusAPIImpl::serial_port_read_some;
    thalamus_api.serial_port_read = ThalamusAPIImpl::serial_port_read;
    thalamus_api.serial_port_write = ThalamusAPIImpl::serial_port_write;

    thalamus_api.streambuf_create = ThalamusAPIImpl::streambuf_create;

    thalamus_api.streambuf_destroy = ThalamusAPIImpl::streambuf_destroy;

    thalamus_api.streambuf_to_span = ThalamusAPIImpl::streambuf_to_span;

    thalamus_api.streambuf_consume = ThalamusAPIImpl::streambuf_consume;

    thalamus_api.streambuf_size = ThalamusAPIImpl::streambuf_size;

    thalamus_api.charspan_release = ThalamusAPIImpl::charspan_release;
    thalamus_api.error_code_message = ThalamusAPIImpl::error_code_message;
    thalamus_api.json_to_string = ThalamusAPIImpl::json_to_string;
    thalamus_api.json_from_string = ThalamusAPIImpl::json_from_string;
    thalamus_api.request_respond = ThalamusAPIImpl::request_respond;
    thalamus_api.json_inc_ref = ThalamusAPIImpl::json_inc_ref;
    thalamus_api.json_dec_ref = ThalamusAPIImpl::json_dec_ref;

    thalamus_api.node_get_node = ThalamusAPIImpl::node_get_node;
    thalamus_api.node_ready_connect = ThalamusAPIImpl::node_ready_connect;

    thalamus_api.node_get_node_disconnect = ThalamusAPIImpl::node_get_node_disconnect;
    thalamus_api.node_ready_disconnect = ThalamusAPIImpl::node_ready_disconnect;
    thalamus_api.node_channels_changed = ThalamusAPIImpl::node_channels_changed;
    
    thalamus_api.node_channels_changed_connect = ThalamusAPIImpl::node_channels_changed_connect;
    thalamus_api.node_channels_changed_disconnect = ThalamusAPIImpl::node_channels_changed_disconnect;

    thalamus_api.node_inc_ref = ThalamusAPIImpl::node_inc_ref;
    thalamus_api.node_dec_ref = ThalamusAPIImpl::node_dec_ref;
    thalamus_api.state_parent = ThalamusAPIImpl::state_parent;

    thalamus_api.state_iter_create = ThalamusAPIImpl::state_iter_create;
    thalamus_api.state_iter_next = ThalamusAPIImpl::state_iter_next;
    thalamus_api.state_iter_key = ThalamusAPIImpl::state_iter_key;
    thalamus_api.state_iter_value = ThalamusAPIImpl::state_iter_value;
    thalamus_api.state_iter_destroy = ThalamusAPIImpl::state_iter_destroy;

    thalamus_api.state_key_of = ThalamusAPIImpl::state_key_of;
    thalamus_api.state_recap_with = ThalamusAPIImpl::state_recap_with;
    thalamus_api.node_get_state = ThalamusAPIImpl::node_get_state;

    thalamus_api.threadpool_post = ThalamusAPIImpl::threadpool_post;
    thalamus_api.node_ready_multithreaded_connect = ThalamusAPIImpl::node_ready_multithreaded_connect;
    thalamus_api.node_ready_offmain = ThalamusAPIImpl::node_ready_offmain;

    thalamus_api.node_predrop_ready = ThalamusAPIImpl::node_predrop_ready;

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
        {"JOYSTICK", new NodeFactory<JoystickNode>()},
        {"SERIAL_TOUCH_SCREEN", new NodeFactory<SerialTouchScreenNode>()},
        {"ARUCO", new NodeFactory<ArucoNode>()}};

    for(auto& ext : extension) {
      THALAMUS_LOG(info) << "Loading " << ext.name();
      //auto library_handle = LoadLibrary("C:\\Thalamus\\ext.dll");

      //auto get_node_factories = reinterpret_cast<get_node_factories_fun>(
      //    ::GetProcAddress(library_handle, "get_node_factories"));

      auto get_node_factories = ext.load<thalamus_get_node_factories>("get_node_factories");
      THALAMUS_ASSERT(get_node_factories, "get_node_factories not found in extension");
      auto factory = get_node_factories(&thalamus_api);
      while(*factory != nullptr) {
        THALAMUS_LOG(info) << "Found " << (*factory)->type;
        auto type_name = to_string((*factory)->type);
        node_factories[type_name] = new ExtNodeFactory(*factory, io_context, outer, &thalamus_api);
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
    
    nodes_connection = nodes->changed.connect(std::bind(&Impl::on_nodes, this, _1, _2, _3));
  }

  ~Impl() {
    nodes_connection.disconnect();
    node_connections.clear();
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
      boost::signals2::scoped_connection conn = node->changed.connect(
          std::bind(&Impl::on_node, this, node.get(), _1, _2, _3));

      std::string type_str = node->at("type");
      auto factory = node_factories.at(type_str);
      
      creating_index = int32_t(index);
      auto node_impl =
          std::shared_ptr<Node>(factory->create(node, io_context, outer));
      creating_index = -1;

      node_connections.insert(node_connections.begin() + index, std::move(conn));
      node_next_type.insert(node_next_type.begin() + index, "");
      node_impls.insert(node_impls.begin() + index, node_impl);
      node_types.insert(node_types.begin() + index, type_str);
      node_configs.insert(node_configs.begin() + index, node);
      node->recap(std::bind(&Impl::on_node, this, node.get(), _1, _2, _3));
    } else {
      auto index = std::get<int64_t>(k);
      node_connections.erase(node_connections.begin() + index);
      node_next_type.erase(node_next_type.begin() + index);
      node_impls.erase(node_impls.begin() + index);
      node_types.erase(node_types.begin() + index);
      node_configs.erase(node_configs.begin() + index);
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
          auto current_node = node_impls.at(size_t(node_index));
          auto current_next_type = node_next_type[size_t(node_index)];

          if(!current_next_type.empty()) {
            node_next_type[size_t(node_index)] = value_str;
          } else {
            node_next_type[size_t(node_index)] = value_str;
            current_node->predrop([this,current_node] {
              boost::asio::post(io_context, [this, current_node] {
                size_t new_node_index = std::numeric_limits<size_t>::max();
                for(size_t i = 0;i < node_impls.size();++i) {
                  if(node_impls[i] == current_node) {
                    new_node_index = i;
                    break;
                  }
                }
                THALAMUS_ASSERT(new_node_index != std::numeric_limits<size_t>::max(), "Node is missing");
                auto node_config = node_configs[new_node_index];

                auto type_str = node_next_type[new_node_index];
                auto factory = node_factories.at(type_str);
                
                node_types.at(new_node_index) = type_str;
                creating_index = int(new_node_index);
                node_impls.at(new_node_index)
                    .reset(factory->create(node_config, io_context, outer));

                creating_index = -1;
                node_next_type[new_node_index] = "";

                auto node_impl = node_impls.at(new_node_index);
                notify([&type_str](
                          auto &selector) { return selector.type() == type_str; },
                      node_impl);
              });
            });
          }
        } else {
          auto node_impl = node_impls.at(size_t(node_index));
          notify([&value_str](
                    auto &selector) { return selector.type() == value_str; },
                node_impl);
        }
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
                             Vulkan vulkan,
                             std::optional<int> thread_policy,
                             std::optional<int> thread_priority)
    : impl(new Impl(nodes, io_context, this, system_time, steady_time, stub, extension, vulkan)) {
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
    impl->io_context.post([callback,value] {
      callback(value);
    });
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
    impl->io_context.post([callback,value] {
      callback(value);
    });
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

ObservableDictPtr NodeGraphImpl::get_node_state(Node* node) {
  for(size_t i = 0;i < impl->node_impls.size();++i) {
    if(impl->node_impls[i].get() == node) {
      return impl->node_configs[i];
    }
  }
  THALAMUS_ABORT("Failed to find node.");
}

VkDevice NodeGraphImpl::get_vulkan_device() {
  return impl->vulkan.device;
}

VkInstance NodeGraphImpl::get_vulkan_instance() {
  return impl->vulkan.instance;
}

VkPhysicalDevice NodeGraphImpl::get_vulkan_physical_device() {
  return impl->vulkan.physical_device;
}

VkQueue NodeGraphImpl::get_vulkan_queue() {
  return impl->vulkan.queue;
}

VkCommandPool NodeGraphImpl::create_vulkan_command_pool() {
  VkCommandPoolCreateInfo cp_ci{};
  cp_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cp_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cp_ci.queueFamilyIndex = impl->vulkan.queue_family_index;
  VkCommandPool result;
  auto success = vkCreateCommandPool(impl->vulkan.device, &cp_ci, nullptr, &result);
  THALAMUS_ASSERT(success == VK_SUCCESS, "vkCreateCommandPool failed");
  return result;
}

void NodeGraphImpl::predrop(std::function<void()> ready) {
  auto drop_count = std::make_shared<size_t>(impl->node_impls.size());
  for(auto& node : impl->node_impls) {
    node->predrop([ready,drop_count,this] {
      boost::asio::post(impl->io_context, [ready,drop_count] {
        --*drop_count;
        if(*drop_count == 0) {
          ready();
        }
      });
    });
  }
}

} // namespace thalamus
