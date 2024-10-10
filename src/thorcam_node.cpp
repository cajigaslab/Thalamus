#include <thorcam_node.hpp>
#include <fstream>
#include <boost/endian/conversion.hpp> 
#include <zlib.h>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/spirit/include/qi.hpp>
#include <numeric>
#include <calculator.hpp>
#include <filesystem>
#include <modalities_util.h>
#include <regex>
#ifdef _WIN32
#else
#include <dlfcn.h>
#endif

#include <tl_camera_load_sdk.h>

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

    void on_frame_ready(const unsigned char* data, int width, int height, std::chrono::steady_clock::time_point now) {
      while (!frame_times.empty() && now - frame_times.front() >= 1s) {
        std::pop_heap(frame_times.begin(), frame_times.end(), [](auto& l, auto& r) { return l > r; });
        frame_times.pop_back();
      }
      if (!frame_times.empty()) {
        auto duration = now - frame_times.front();
        auto duration_seconds = double(duration.count())/decltype(duration)::period::den;
        framerate = frame_times.size()/duration_seconds;
      } else {
        framerate = 0;
      }
      frame_times.push_back(now);
      std::push_heap(frame_times.begin(), frame_times.end(), [](auto& l, auto& r) { return l > r; });


      this->time = now.time_since_epoch();
      this->data.clear();
      this->data.emplace_back(data, data + width*height);
      this->width = width;
      this->height = height;
      this->has_image = true;
      this->has_analog = true;
      analog_impl.inject({ std::span<const double>(&framerate, &framerate + 1) }, { std::chrono::nanoseconds(size_t(1e9 / target_framerate)) }, {""});
    }

    std::string selected_camera;
    void* camera_handle = nullptr;
    bool streaming = false;

    void initialize_camera(bool apply_state = false) {
      std::string camera = state->at("Camera");
      auto i = std::find(camera_names.begin(), camera_names.end(), camera);
      if(i == camera_names.end()) {
        selected_camera = "";
        camera_handle = nullptr;
      } else {
        selected_camera = *i;
        auto error = tl_camera_open_camera(selected_camera.c_str(), &camera_handle);
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
          auto value = variant_cast<long long>(state->at("ExposureTime"));
          auto error = tl_camera_set_exposure_time(camera_handle, value*1000);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_exposure_time failed %s", tl_camera_get_last_error());
        }

        if(state->contains("AcquisitionFrameRate")) {
          auto value = variant_cast<double>(state->at("AcquisitionFrameRate"));
          auto error = tl_camera_set_frame_rate_control_value(camera_handle, value);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_frame_rate_control_value failed %s", tl_camera_get_last_error());
        }

        if(state->contains("Gain")) {
          auto value = variant_cast<long long>(state->at("Gain"));
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

        int exposure_us;
        error = tl_camera_get_exposure_time(camera_handle, &exposure_us);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_exposure_time failed %s", tl_camera_get_last_error());
        (*state)["ExposureTime"].assign(exposure_us/1000);

        int fps;
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

    void on_frame_available(void* sender, unsigned short* image_buffer, int frame_count, unsigned char* metadata, int metadata_size_in_bytes, void* context) {
      auto self = reinterpret_cast<Impl*>(context);
      boost::asio::post(self->io_context, [self] {
          auto outer = self->outer;
          self->data = image_buffer;
          outer->ready(outer);
      });
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
    }

    void stop_stream() {
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
        int exposure_us;
        error = tl_camera_get_exposure_time(camera_handle, &exposure_us);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_exposure_time failed %s", tl_camera_get_last_error());
        (*state)["ExposureTime"].assign(exposure_us/1000);
      } else if (key_str == "AcquisitionFrameRate") {
        auto value = variant_cast<double>(v);
        if(!streaming) {
          auto error = tl_camera_set_frame_rate_control_value(camera_handle, value);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_frame_rate_control_value failed %s", tl_camera_get_last_error());
        }
        error = tl_camera_get_frame_rate_control_value(camera_handle, &value);
        THALAMUS_ASSERT(error == 0, "tl_camera_get_frame_rate_control_value failed %s", tl_camera_get_last_error());
        (*state)["AcquisitionFrameRate"].assign(value);
      } else if (key_str == "Gain") {
        auto value = variant_cast<long long>(v);
        if(!streaming) {
          auto error = tl_camera_set_gain(camera_handle, value);
          THALAMUS_ASSERT(error == 0, "tl_camera_set_gain failed %s", tl_camera_get_last_error());
        }
        error = tl_camera_get_gain(camera_handle, &value);
        THALAMUS_ASSERT(error == 0, "tl_camerga_get_gain failed %s", tl_camera_get_last_error());
        (*state)["Gain"].assign(value);
      }
    }
  };
  calculator::parser<std::string::const_iterator> ThorcamNode::Impl::Cti::parser;        // Our grammar

  std::vector<std::unique_ptr<ThorcamNode::Impl::Cti>> ThorcamNode::Impl::ctis;
  std::mutex ThorcamNode::Impl::ctis_mutex;
  bool ThorcamNode::Impl::ctis_loaded = false;

  ThorcamNode::ThorcamNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*)
    : impl(new Impl(state, io_context, this)) {}

  ThorcamNode::~ThorcamNode() {}

  std::string ThorcamNode::type_name() {
    return "GENICAM";
  }

  ImageNode::Plane ThorcamNode::plane(int i) const {
    return impl->data.at(i);
  }

  size_t ThorcamNode::num_planes() const {
    return impl->data.size();
  }

  ImageNode::Format ThorcamNode::format() const {
    return ImageNode::Format::Gray;
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
    error = tl_camera_discover_available_cameras(camera_ids, 1024)
    if(error) {
      auto error_str = tl_camera_get_last_error():
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
    auto error = tl_camera_sdk_dll_terminate();
    if(error) {
      THALAMUS_LOG(info) << "Thorlabs camera API not found, feature disabled: " << error;
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


  static bool ThorcamNode::Impl::loaded = false;
}
