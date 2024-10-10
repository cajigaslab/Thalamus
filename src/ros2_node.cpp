#include <ros2_node.h>
#include <image_node.hpp>
#include <text_node.h>
#include <distortion_node.h>
#include <oculomatic_node.h>
#include <util.h>
#include <absl/strings/str_format.h>
#include <absl/time/time.h>
#include <modalities_util.h>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/vec_operations.hpp>
#include <boost/qvm/quat_access.hpp>
#include <boost/qvm/quat_operations.hpp>
#include <boost/qvm/quat_vec_operations.hpp>
#include <boost/qvm/swizzle.hpp>

#ifdef _WIN32
#include <winsock2.h>
#elif __APPLE__
#include <arpa/inet.h>
#else
#include <endian.h>
#define htonll(x) htobe64(x)
#endif

extern "C" {
typedef int (*thalamus_ros2_bridge_start_t)();
typedef int (*thalamus_ros2_bridge_stop_t)();
typedef int (*thalamus_ros2_bridge_create_image_publisher_t)(const char* topic);
typedef int (*thalamus_ros2_bridge_create_camera_info_publisher_t)(const char* topic);
typedef int (*thalamus_ros2_bridge_create_gaze_publisher_t)(const char* topic);
typedef int (*thalamus_ros2_bridge_publish_image_t)(int publisher,
                                              unsigned long long timestamp_ns,
                                              int width, int height, 
                                              const char* encoding, bool is_bigendian, 
                                              int step, const unsigned char* data);
typedef int (*thalamus_ros2_bridge_publish_camera_info_t)(int publisher,
                                                    unsigned long long timestamp_ns,
                                                    int width, int height, 
                                                    const char* model, 
                                                    const double* d, int num_d, const double* k);
typedef int (*thalamus_ros2_bridge_publish_gaze_t)(int publisher,
                                             unsigned long long timestamp_ns,
                                             float x, float y, int width, int height, 
                                             int diameter, int i);
typedef int (*thalamus_ros2_bridge_broadcast_transform_t)(unsigned long long timestamp_ns,
                                             const char* parent_frame, const char* child_frame,
                                             const double* translation, const double* rotation);
}

thalamus_ros2_bridge_start_t thalamus_ros2_bridge_start;
thalamus_ros2_bridge_stop_t thalamus_ros2_bridge_stop;
thalamus_ros2_bridge_create_image_publisher_t thalamus_ros2_bridge_create_image_publisher;
thalamus_ros2_bridge_create_camera_info_publisher_t thalamus_ros2_bridge_create_camera_info_publisher;
thalamus_ros2_bridge_create_gaze_publisher_t thalamus_ros2_bridge_create_gaze_publisher;
thalamus_ros2_bridge_publish_image_t thalamus_ros2_bridge_publish_image;
thalamus_ros2_bridge_publish_camera_info_t thalamus_ros2_bridge_publish_camera_info;
thalamus_ros2_bridge_publish_gaze_t thalamus_ros2_bridge_publish_gaze;
thalamus_ros2_bridge_broadcast_transform_t thalamus_ros2_bridge_broadcast_transform;

namespace thalamus {
  struct Ros2Node::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    thalamus::map<std::string, boost::signals2::scoped_connection> source_connections;
    NodeGraph* graph;
    Ros2Node* outer;

    Impl(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph, Ros2Node* outer)
      : state(state)
      , graph(graph)
      , outer(outer) {
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [] {});
    }

    struct PublishInfo {
      int camera_info_publisher = -1;
      int image_publisher = -1;
      int gaze_publisher = -1;
      int eye = 0;
      std::string parent_frame;
      std::string child_frame;
      std::weak_ptr<Node> node;
      ImageNode* image;
      DistortionNode* distortion;
      OculomaticNode* oculomatic;
      MotionCaptureNode* mocap;
    };

    void on_distortion_data(Node*, std::shared_ptr<PublishInfo> info) {
      if(!info->distortion->has_image_data()) {
        return;
      }

      if(info->camera_info_publisher != -1) {
        auto& mat = info->distortion->camera_matrix();
        auto span = info->distortion->distortion_coefficients();
        auto time = info->distortion->time();
        auto width = info->distortion->width();
        auto height = info->distortion->height();
        auto model = "plumb_bob";
        const double* k = mat.ptr<double>(0);
        const double* d = span.data();
        int num_d = span.size();
        thalamus_ros2_bridge_publish_camera_info(info->camera_info_publisher, time.count(), width, height, model, d, num_d, k);
      } 
      if (info->image_publisher != -1) {
        auto time = info->distortion->time();
        auto width = info->distortion->width();
        auto height = info->distortion->height();
        auto data = info->distortion->plane(0);
        thalamus_ros2_bridge_publish_image(info->image_publisher, time.count(), width, height, "mono8", true, width, data.data());
      }
    }

    void on_oculomatic_data(Node*, std::shared_ptr<PublishInfo> info) {
      if(info->gaze_publisher != -1 && info->oculomatic->has_analog_data()) {
        auto x = info->oculomatic->data(0);
        auto y = info->oculomatic->data(1);
        auto diameter = info->oculomatic->data(2);

        auto time = info->oculomatic->time();
        auto width = info->oculomatic->width();
        auto height = info->oculomatic->height();

        thalamus_ros2_bridge_publish_gaze(info->gaze_publisher, time.count(), x.front(), y.front(), width, height, diameter.front(), info->eye);
      } 
      if (info->image_publisher != -1 && info->image->has_image_data()) {
        auto time = info->oculomatic->time();
        auto width = info->oculomatic->width();
        auto height = info->oculomatic->height();
        auto data = info->oculomatic->plane(0);
        thalamus_ros2_bridge_publish_image(info->image_publisher, time.count(), width, height, "mono8", true, width, data.data());
      }
    }

    void on_image_data(Node*, std::shared_ptr<PublishInfo> info) {
      if (info->image_publisher != -1 && info->image->has_image_data()) {
        auto time = info->image->time();
        auto width = info->image->width();
        auto height = info->image->height();
        auto data = info->image->plane(0);
        thalamus_ros2_bridge_publish_image(info->image_publisher, time.count(), width, height, "mono8", true, width, data.data());
      }
    }

    void on_mocap_data(Node*, std::shared_ptr<PublishInfo> info) {
      if (info->mocap->has_motion_data()) {
        auto time = info->mocap->time();
        auto segments = info->mocap->segments();
        if(segments.empty()) {
          return;
        }
        auto& segment = segments.front();
        double translation[] = {boost::qvm::X(segment.position), boost::qvm::Y(segment.position), boost::qvm::Z(segment.position)};
        double rotation[] = {boost::qvm::X(segment.rotation), boost::qvm::Y(segment.rotation), boost::qvm::Z(segment.rotation), boost::qvm::S(segment.rotation)};
        
        thalamus_ros2_bridge_broadcast_transform(time.count(), info->parent_frame.c_str(), info->child_frame.c_str(), translation, rotation);
      }
    }

    void on_source_change(std::shared_ptr<PublishInfo> info, ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto k_str = std::get<std::string>(k);
      if(k_str == "Gaze Topic") {
        auto v_str = std::get<std::string>(v);
        if(v_str.empty()) {
          return;
        }
        auto locked_source = info->node.lock();
        if (!locked_source) {
          return;
        }
        info->gaze_publisher = thalamus_ros2_bridge_create_gaze_publisher(v_str.c_str());
      } else if (k_str == "Image Topic") {
        auto v_str = std::get<std::string>(v);
        if(v_str.empty()) {
          return;
        }
        auto locked_source = info->node.lock();
        if (!locked_source) {
          return;
        }
        info->image_publisher = thalamus_ros2_bridge_create_image_publisher(v_str.c_str());
      } else if (k_str == "Camera Info Topic") {
        auto v_str = std::get<std::string>(v);
        if(v_str.empty()) {
          return;
        }
        auto locked_source = info->node.lock();
        if (!locked_source) {
          return;
        }
        info->camera_info_publisher = thalamus_ros2_bridge_create_camera_info_publisher(v_str.c_str());
      } else if (k_str == "Eye") {
        info->eye = std::get<long long>(v);
      } else if (k_str == "Parent Frame") {
        info->parent_frame = std::get<std::string>(v);
      } else if (k_str == "Child Frame") {
        info->child_frame = std::get<std::string>(v);
      }
    }

    void on_sources_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto node_name = std::get<std::string>(k);
      if(a == ObservableCollection::Action::Set) {
        auto v_dict = std::get<ObservableDictPtr>(v);
        graph->get_node(node_name, [&,node_name,v_dict](auto source) {
          auto locked_source = source.lock();
          if (!locked_source) {
            return;
          }

          auto publish_info = std::make_shared<PublishInfo>();
          publish_info->node = source;
          
          if (node_cast<DistortionNode*>(locked_source.get()) != nullptr) {
            publish_info->distortion = node_cast<DistortionNode*>(locked_source.get());
            publish_info->image = node_cast<ImageNode*>(locked_source.get());
            auto source_connection = locked_source->ready.connect(std::bind(&Impl::on_distortion_data, this, _1, publish_info));
            source_connections[node_name] = std::move(source_connection);
          } else if (node_cast<OculomaticNode*>(locked_source.get()) != nullptr) {
            publish_info->oculomatic = node_cast<OculomaticNode*>(locked_source.get());
            publish_info->image = node_cast<ImageNode*>(locked_source.get());
            auto source_connection = locked_source->ready.connect(std::bind(&Impl::on_oculomatic_data, this, _1, publish_info));
            source_connections[node_name] = std::move(source_connection);
          } else if (node_cast<ImageNode*>(locked_source.get()) != nullptr) {
            publish_info->image = node_cast<ImageNode*>(locked_source.get());
            auto source_connection = locked_source->ready.connect(std::bind(&Impl::on_image_data, this, _1, publish_info));
            source_connections[node_name] = std::move(source_connection);
          }
          
          if (node_cast<MotionCaptureNode*>(locked_source.get()) != nullptr) {
            publish_info->mocap = node_cast<MotionCaptureNode*>(locked_source.get());
            auto source_connection = locked_source->ready.connect(std::bind(&Impl::on_mocap_data, this, _1, publish_info));
            source_connections[node_name] = std::move(source_connection);
          }

          source_mapping_connections[node_name] = v_dict->changed.connect(std::bind(&Impl::on_source_change, this, publish_info, _1, _2, _3));
          v_dict->recap(std::bind(&Impl::on_source_change, this, publish_info, _1, _2, _3));
        });
      } else if(a == ObservableCollection::Action::Delete) {
        source_mapping_connections.erase(node_name);
        sources_connections.erase(node_name);
      }
    }

    std::map<std::string, boost::signals2::scoped_connection> source_mapping_connections;
    std::map<std::string, std::pair<boost::signals2::scoped_connection, boost::signals2::scoped_connection>> sources_connections;
    std::vector<std::weak_ptr<AnalogNode>> sources;
    boost::signals2::scoped_connection sources_state_connection;

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Sources") {
        auto sources_dict = std::get<ObservableDictPtr>(v);
        sources_connections.clear();
        sources.clear();
        sources_state_connection = sources_dict->changed.connect(std::bind(&Impl::on_sources_change, this, _1, _2, _3));
        sources_dict->recap(std::bind(&Impl::on_sources_change, this, _1, _2, _3));
      }
    }
  };

  Ros2Node::Ros2Node(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph) : impl(new Impl(state, io_context, graph, this)) {}

  Ros2Node::~Ros2Node() {}

  std::string Ros2Node::type_name() {
    return "ROS2";
  }

  std::span<const double> Ros2Node::data(int) const {
    return std::span<const double>();
  }

  int Ros2Node::num_channels() const {
    return 0;
  }

  std::chrono::nanoseconds Ros2Node::sample_interval(int) const {
	  THALAMUS_ASSERT(false);
    return 1s;
  }

  std::chrono::nanoseconds Ros2Node::time() const {
	  THALAMUS_ASSERT(false);
    return 0ns;
  }

  std::string_view Ros2Node::name(int) const {
	  THALAMUS_ASSERT(false);
    return std::string_view();
  }

  void Ros2Node::inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
    THALAMUS_ASSERT(false);
  }

  template<typename T>
  T load_function(void* library_handle, const std::string& name) {
    auto result = reinterpret_cast<T>(dlsym(library_handle, name.c_str()));
    if(!result) {
      THALAMUS_LOG(info) << "Failed to load " << name;
    }
    return result;
  }

#define LOAD_FUNC(name) name = load_function<name##_t>(library_handle, #name);if(!name) { return false; }

  bool Ros2Node::prepare() {
	  auto library_handle = dlopen("libthalamus_ros2_bridge.so", RTLD_NOW);
    if(library_handle == nullptr) {
      auto error = dlerror();
      THALAMUS_LOG(info) << "Failed to load libthalamus_ros2_bridge.so: " << error;
      return false;
    }

    LOAD_FUNC(thalamus_ros2_bridge_start);
    LOAD_FUNC(thalamus_ros2_bridge_stop);
    LOAD_FUNC(thalamus_ros2_bridge_create_image_publisher);
    LOAD_FUNC(thalamus_ros2_bridge_create_camera_info_publisher);
    LOAD_FUNC(thalamus_ros2_bridge_create_gaze_publisher);
    LOAD_FUNC(thalamus_ros2_bridge_publish_image);
    LOAD_FUNC(thalamus_ros2_bridge_publish_camera_info);
    LOAD_FUNC(thalamus_ros2_bridge_publish_gaze);
    LOAD_FUNC(thalamus_ros2_bridge_broadcast_transform);

    thalamus_ros2_bridge_start();

    return true;
  }

  void Ros2Node::cleanup() {
    thalamus_ros2_bridge_stop();
  }
}
