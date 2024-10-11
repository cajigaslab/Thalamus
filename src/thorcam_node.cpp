#include <thorcam_node.hpp>
#include <modalities_util.h>
#ifdef _WIN32
#else
#include <dlfcn.h>
#endif

#include <tl_camera_sdk.h>
#include <tl_camera_sdk_load.h>

namespace thalamus {
  using namespace std::chrono_literals;
  template <class...> constexpr std::false_type always_false{};

  template <typename T>
  struct Caster {
    template<typename M>
    T operator()(M arg) {
      if constexpr (std::is_convertible<M, T>()) {
        return static_cast<T>(arg);
      } else {
        THALAMUS_ASSERT(false, "Not convertable");
      }
    }
  };

  template<typename T, typename... VARIANTS>
  static T variant_cast(const std::variant<VARIANTS...>& arg) {
    return std::visit(Caster<T>{}, arg);
  }

  static std::vector<std::string> camera_names;

  struct ThorcamNode::Impl {
    boost::asio::io_context& io_context;
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::signals2::scoped_connection options_connection;
    bool is_running = false;
    ThorcamNode* outer;
    std::chrono::nanoseconds time;
    std::thread ffmpeg_thread;
    bool running = false;
    thalamus_grpc::Image image;
    std::atomic_bool frame_pending;
    std::vector<unsigned char> intermediate;
    thalamus::vector<Plane> data;
    std::chrono::nanoseconds frame_interval;
    Format format;
    size_t width;
    size_t height;
    AnalogNodeImpl analog_impl;
    bool has_analog = false;
    bool has_image = false;

    std::string default_camera;
    boost::signals2::scoped_connection frame_connection;
    std::once_flag load_ctis_flag;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, ThorcamNode* outer)
      : io_context(io_context)
      , state(state)
      , outer(outer) {
      using namespace std::placeholders;

      analog_impl.inject({ {std::span<double const>()} }, { 0ns }, {""});

      analog_impl.ready.connect([outer](Node*) {
        outer->ready(outer);
      });

      if (default_camera.empty()) {
        return;
      }
      if(!state->contains("Camera")) {
        (*state)["Camera"].assign(default_camera);
      } else {
        initialize_camera(true);
      }

      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [&] {});
    }

    std::vector<std::chrono::steady_clock::time_point> frame_times;
    double framerate = 0;
    double target_framerate = 0;

    std::string selected_camera;
    void* camera_handle = nullptr;
    bool streaming = false;

    void initialize_camera(bool apply_state = false) {
      if(camera_handle) {
        auto error = tl_camera_close_camera(camera_handle);
        THALAMUS_ASSERT(error == 0, "tl_camera_close_camera failed %s", tl_camera_get_last_error());
        selected_camera = "";
        camera_handle = nullptr;
      }

      std::string camera = state->at("Camera");
      auto i = std::find(camera_names.begin(), camera_names.end(), camera);
      if(i == camera_names.end()) {
        selected_camera = "";
        camera_handle = nullptr;
      } else {
        selected_camera = *i;
        std::unique_ptr<char> c_str(new char[selected_camera.size()+1]);
        strcpy(c_str.get(), selected_camera.c_str());
        auto error = tl_camera_open_camera(c_str.get(), &camera_handle);
        THALAMUS_ASSERT(error == 0, "tl_camera_open_camera failed %s", tl_camera_get_last_error());
      }

      if(!camera_handle) {
        return;
      }

      int ul_x_min, ul_y_min, lr_x_min, lr_y_min, ul_x_max, ul_y_max, lr_x_max, lr_y_max;
      auto error = tl_camera_get_roi_range(camera_handle, &ul_x_min, &ul_y_min, &lr_x_min, &lr_y_min, &ul_x_max, &ul_y_max, &lr_x_max, &lr_y_max);
      THALAMUS_ASSERT(error == 0, "tl_camera_get_roi_range failed %s", tl_camera_get_last_error());

      (*state)["WidthMax"].assign(lr_x_max - ul_x_min);
      (*state)["HeightMax"].assign(lr_y_max - ul_y_min);

      if(apply_state) {
        if(state->contains("OffsetX") && state->contains("OffsetY") && state->contains("Width") && state->contains("Height")) {
          long long int width = state->at("Width");
          long long int height = state->at("Height");
          long long int x = state->at("OffsetX");
          long long int y = state->at("OffsetY");

          auto error = tl_camera_set_roi(camera_handle, x, y, x+width, x+height);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_roi failed %s", tl_camera_get_last_error());
        }

        if(state->contains("ExposureTime")) {
          long long value = state->at("ExposureTime");
          auto error = tl_camera_set_exposure_time(camera_handle, value*1000);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_exposure_time failed %s", tl_camera_get_last_error());
        }

        if(state->contains("AcquisitionFrameRate")) {
          double value = state->at("AcquisitionFrameRate");
          auto error = tl_camera_set_frame_rate_control_value(camera_handle, value);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_frame_rate_control_value failed %s", tl_camera_get_last_error());
        }

        if(state->contains("Gain")) {
          long long value = state->at("Gain");
          auto error = tl_camera_set_gain(camera_handle, value);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_gain failed %s", tl_camera_get_last_error());
        }
      } else {
        int ul_x, ul_y, lr_x, lr_y;
        auto error = tl_camera_get_roi(camera_handle, &ul_x, &ul_y, &lr_x, &lr_y);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_roi failed %s", tl_camera_get_last_error());

        (*state)["OffsetX"].assign(ul_x);
        (*state)["OffsetY"].assign(ul_y);
        (*state)["Width"].assign(lr_x - ul_x);
        (*state)["Height"].assign(lr_y - ul_y);

        long long exposure_us;
        error = tl_camera_get_exposure_time(camera_handle, &exposure_us);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_exposure_time failed %s", tl_camera_get_last_error());
        (*state)["ExposureTime"].assign(exposure_us/1000);

        double fps;
        error = tl_camera_get_frame_rate_control_value(camera_handle, &fps);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_frame_rate_control_value failed %s", tl_camera_get_last_error());
        (*state)["AcquisitionFrameRate"].assign(fps);

        int gain;
        error = tl_camera_get_gain(camera_handle, &gain);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_gain failed %s", tl_camera_get_last_error());
        (*state)["Gain"].assign(fps);
      }
    }

    void update_roi() {
      if(!streaming) {
        if(state->contains("OffsetX") && state->contains("OffsetY") && state->contains("Width") && state->contains("Height")) {
          long long int width = state->at("Width");
          long long int height = state->at("Height");
          long long int x = state->at("OffsetX");
          long long int y = state->at("OffsetY");

          auto error = tl_camera_set_roi(camera_handle, x, y, x+width, x+height);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_roi failed %s", tl_camera_get_last_error());
        }
      }
      int ul_x, ul_y, lr_x, lr_y;
      auto error = tl_camera_get_roi(camera_handle, &ul_x, &ul_y, &lr_x, &lr_y);
      THALAMUS_ASSERT(error == 0, "tl_camera_get_roi failed %s", tl_camera_get_last_error());

      (*state)["OffsetX"].assign(ul_x);
      (*state)["OffsetY"].assign(ul_y);
      (*state)["Width"].assign(lr_x - ul_x);
      (*state)["Height"].assign(lr_y - ul_y);
    }

    static void on_frame_available(void* sender, unsigned short* image_buffer, int frame_count, unsigned char* metadata, int metadata_size_in_bytes, void* context) {
      auto self = reinterpret_cast<Impl*>(context);
      auto time = std::chrono::steady_clock::now();
      std::promise<void> promise;
      auto future = promise.get_future();
      boost::asio::post(self->io_context, [self,time,image_buffer,promise=std::move(promise)] () mutable {
        self->time = time.time_since_epoch();
        auto bytes = reinterpret_cast<unsigned char*>(image_buffer);
        self->data.assign(1, std::span<unsigned char>(bytes, bytes + self->width*self->height));
        auto outer = self->outer;
        self->has_image = true;
        self->has_analog = false;
        outer->ready(outer);
        promise.set_value();
      });
      future.wait();
    }

    void start_stream() {
      auto error = tl_camera_set_frames_per_trigger_zero_for_unlimited(camera_handle, 0);
      THALAMUS_ASSERT(error == 0, "tl_camera_set_frames_per_trigger_zero_for_unlimited failed %s", tl_camera_get_last_error());
      
      error = tl_camera_set_frame_available_callback(camera_handle, Impl::on_frame_available, this);
      THALAMUS_ASSERT(error == 0, "tl_camera_set_frame_available_callback failed %s", tl_camera_get_last_error());

      error = tl_camera_arm(camera_handle, 2);
      THALAMUS_ASSERT(error == 0, "tl_camera_arm failed %s", tl_camera_get_last_error());

      error = tl_camera_issue_software_trigger(camera_handle);
      THALAMUS_ASSERT(error == 0, "tl_camera_issue_software_trigger failed %s", tl_camera_get_last_error());

      int ul_x, ul_y, lr_x, lr_y;
      error = tl_camera_get_roi(camera_handle, &ul_x, &ul_y, &lr_x, &lr_y);
      THALAMUS_ASSERT(error == 0, "tl_camera_get_roi failed %s", tl_camera_get_last_error());
      width = lr_x - ul_x;
      height = lr_y - ul_y;

      error = tl_camera_get_frame_rate_control_value(camera_handle, &target_framerate);
      THALAMUS_ASSERT(error == 0, "tl_camera_get_frame_rate_control_value failed %s", tl_camera_get_last_error());
    }

    void stop_stream() {
      auto error = tl_camera_disarm(camera_handle);
      THALAMUS_ASSERT(error == 0, "tl_camera_disarm failed %s", tl_camera_get_last_error());
    }

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Camera") {
        initialize_camera();
        return;
      } else if (key_str == "Running") {
        running = variant_cast<bool>(v);
        if(camera_handle) {
          if(running) {
            start_stream();
          } else {
            stop_stream();
          }
        }
        return;
      }
      
      if(!camera_handle) {
        return;
      }

      if (key_str == "Width") {
        update_roi();
      } else if (key_str == "Height") {
        update_roi();
      } else if (key_str == "OffsetX") {
        update_roi();
      } else if (key_str == "OffsetY") {
        update_roi();
      } else if (key_str == "ExposureTime") {
        auto value = variant_cast<double>(v);
        if(!streaming) {
          auto error = tl_camera_set_exposure_time(camera_handle, value*1000);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_exposure_time failed %s", tl_camera_get_last_error());
        }
        long long exposure_us;
        auto error = tl_camera_get_exposure_time(camera_handle, &exposure_us);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_exposure_time failed %s", tl_camera_get_last_error());
        (*state)["ExposureTime"].assign(exposure_us/1000);
      } else if (key_str == "AcquisitionFrameRate") {
        auto value = variant_cast<double>(v);
        if(!streaming) {
          auto error = tl_camera_set_frame_rate_control_value(camera_handle, value);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_frame_rate_control_value failed %s", tl_camera_get_last_error());
        }
        auto error = tl_camera_get_frame_rate_control_value(camera_handle, &value);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_frame_rate_control_value failed %s", tl_camera_get_last_error());
        (*state)["AcquisitionFrameRate"].assign(value);
      } else if (key_str == "Gain") {
        int value = variant_cast<long long>(v);
        if(!streaming) {
          auto error = tl_camera_set_gain(camera_handle, value);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_gain failed %s", tl_camera_get_last_error());
        }
        auto error = tl_camera_get_gain(camera_handle, &value);
        THALAMUS_ASSERT(error == 0, "tl_camerga_get_gain failed %s", tl_camera_get_last_error());
        (*state)["Gain"].assign(value);
      }
    }
  };

  ThorcamNode::ThorcamNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*)
    : impl(new Impl(state, io_context, this)) {}

  ThorcamNode::~ThorcamNode() {}

  std::string ThorcamNode::type_name() {
    return "THORCAM";
  }

  ImageNode::Plane ThorcamNode::plane(int i) const {
    return impl->data.at(i);
  }

  size_t ThorcamNode::num_planes() const {
    return impl->data.size();
  }

  ImageNode::Format ThorcamNode::format() const {
    return ImageNode::Format::Gray16;
  }

  size_t ThorcamNode::width() const {
    return impl->width;
  }

  size_t ThorcamNode::height() const {
    return impl->height;
  }

  void ThorcamNode::inject(const thalamus_grpc::Image&) {
    THALAMUS_ASSERT(false, "Unimplemented");
  }

  std::chrono::nanoseconds ThorcamNode::time() const {
    return impl->time;
  }

  std::chrono::nanoseconds ThorcamNode::frame_interval() const {
    return std::chrono::nanoseconds(size_t(1e9/impl->target_framerate));
  }


  bool ThorcamNode::prepare() {
    auto error = tl_camera_sdk_dll_initialize();
    if(error) {
      THALAMUS_LOG(info) << "Thorlabs camera API not found, feature disabled: " << error;
      return false;
    }

    error = tl_camera_open_sdk();
    if(error) {
      THALAMUS_LOG(info) << "tl_camera_open_sdk failed, feature disabled: " << error;
      return false;
    }

    char ids[1024];
    error = tl_camera_discover_available_cameras(ids, 1024);
    if(error) {
      auto error_str = tl_camera_get_last_error();
      THALAMUS_LOG(info) << "Camera discovery failed: " << error << ", " << error_str;
      return false;
    }
    
    if(ids[0] == 0) {
      THALAMUS_LOG(info) << "No cameras found, feature disabled";
      return false;
    }

    camera_names = absl::StrSplit(ids, ' ');

    //error = tl_camera_set_camera_connect_callback(ThorcamNode::Impl::on_camera_connect, nullptr);
    //if(error) {
    //  auto error_str = tl_camera_get_last_error():
    //  THALAMUS_LOG(info) << "Failed to set camera connect callback: " << error << ", " << error_str;
    //  return false;
    //}

    //error = tl_camera_set_camera_disconnect_callback(ThorcamNode::Impl::on_camera_disconnect, nullptr);
    //if(error) {
    //  auto error_str = tl_camera_get_last_error():
    //  THALAMUS_LOG(info) << "Failed to set camera connect callback: " << error << ", " << error_str;
    //  return false;
    //}
    return true;
  }

  void ThorcamNode::cleanup() {
    auto error = tl_camera_close_sdk();
    if(error) {
      THALAMUS_LOG(info) << "tl_camera_close_sdk failed";
      return;
    }

    error = tl_camera_sdk_dll_terminate();
    if(error) {
      THALAMUS_LOG(info) << "tl_camera_sdk_dll_terminate failed" << error;
    }
  }

  std::span<const double> ThorcamNode::data(int index) const {
    return impl->analog_impl.data(index);
  }

  int ThorcamNode::num_channels() const {
    return impl->analog_impl.num_channels();
  }

  std::chrono::nanoseconds ThorcamNode::sample_interval(int channel) const {
    return impl->analog_impl.sample_interval(channel);
  }

  static const std::string EMPTY = "";
  static const std::string ACTUAL_FRAMERATE = "Framerate";
  static std::vector<std::string> names = {ACTUAL_FRAMERATE};

  std::string_view ThorcamNode::name(int channel) const {
    return names.at(channel);
  }

  void ThorcamNode::inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& interval, const thalamus::vector<std::string_view>& names) {
    impl->has_analog = true;
    impl->has_image = false;
    impl->analog_impl.inject(data, interval, names);
  }

  bool ThorcamNode::has_analog_data() const {
    return impl->has_analog;
  }

  bool ThorcamNode::has_image_data() const {
    return impl->has_image;
  }

  std::span<const std::string> ThorcamNode::get_recommended_channels() const {
    return std::span<const std::string>(names.begin(), names.end());
  }

  boost::json::value ThorcamNode::process(const boost::json::value& request) {
    if(request.kind() != boost::json::kind::string) {
      return boost::json::value();
    }

    auto request_str = request.as_string();
    if(request_str == "get_cameras") {
      boost::json::array result;
      for(auto name : camera_names) {
        result.push_back(boost::json::string(name));
      }
      return result;
    }
    return boost::json::value();
  }
  size_t ThorcamNode::modalities() const { return infer_modalities<ThorcamNode>(); }
}
