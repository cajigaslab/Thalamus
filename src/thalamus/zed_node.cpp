#include <thalamus/tracing.hpp>
#include <thalamus/modalities_util.hpp>
#include <thalamus/util.hpp>
#include <thalamus/zed_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/json.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// ZED BODY_18 joint names (indices 0-17)
static constexpr size_t ZED_BODY18_COUNT = 18;
static constexpr std::array<std::string_view, ZED_BODY18_COUNT>
    ZED_BODY18_NAMES = {
        "PELVIS",          // 0
        "NAVAL_SPINE",     // 1
        "CHEST_SPINE",     // 2
        "NECK",            // 3
        "LEFT_CLAVICLE",   // 4
        "LEFT_SHOULDER",   // 5
        "LEFT_ELBOW",      // 6
        "LEFT_WRIST",      // 7
        "RIGHT_CLAVICLE",  // 8
        "RIGHT_SHOULDER",  // 9
        "RIGHT_ELBOW",     // 10
        "RIGHT_WRIST",     // 11
        "LEFT_HIP",        // 12
        "LEFT_KNEE",       // 13
        "LEFT_ANKLE",      // 14
        "RIGHT_HIP",       // 15
        "RIGHT_KNEE",      // 16
        "RIGHT_ANKLE",     // 17
};

// ZED BODY_34 joint names (indices 0-33)
// Note: ordering differs from BODY_18 starting at index 4.
static constexpr size_t ZED_BODY34_COUNT = 34;
static constexpr std::array<std::string_view, ZED_BODY34_COUNT>
    ZED_BODY34_NAMES = {
        "PELVIS",          // 0
        "NAVAL_SPINE",     // 1
        "CHEST_SPINE",     // 2
        "NECK",            // 3
        "LEFT_CLAVICLE",   // 4
        "LEFT_SHOULDER",   // 5
        "LEFT_ELBOW",      // 6
        "LEFT_WRIST",      // 7
        "LEFT_HAND",       // 8
        "LEFT_HANDTIP",    // 9
        "LEFT_THUMB",      // 10
        "RIGHT_CLAVICLE",  // 11
        "RIGHT_SHOULDER",  // 12
        "RIGHT_ELBOW",     // 13
        "RIGHT_WRIST",     // 14
        "RIGHT_HAND",      // 15
        "RIGHT_HANDTIP",   // 16
        "RIGHT_THUMB",     // 17
        "LEFT_HIP",        // 18
        "LEFT_KNEE",       // 19
        "LEFT_ANKLE",      // 20
        "LEFT_FOOT",       // 21
        "RIGHT_HIP",       // 22
        "RIGHT_KNEE",      // 23
        "RIGHT_ANKLE",     // 24
        "RIGHT_FOOT",      // 25
        "HEAD",            // 26
        "NOSE",            // 27
        "LEFT_EYE",        // 28
        "LEFT_EAR",        // 29
        "RIGHT_EYE",       // 30
        "RIGHT_EAR",       // 31
        "LEFT_HEEL",       // 32
        "RIGHT_HEEL",      // 33
};

/// Return the name for joint index i, selecting the correct table based on
/// total_joints (34 → BODY_34 names, otherwise → BODY_18 names).
static std::string joint_name(size_t i, size_t total_joints) {
  if (total_joints == ZED_BODY34_COUNT) {
    if (i < ZED_BODY34_COUNT) {
      return std::string(ZED_BODY34_NAMES[i]);
    }
  } else {
    if (i < ZED_BODY18_COUNT) {
      return std::string(ZED_BODY18_NAMES[i]);
    }
  }
  return "JOINT_" + std::to_string(i);
}

// ---------------------------------------------------------------------------
// Helper: safe JSON field extraction with warnings
// ---------------------------------------------------------------------------


/// Attempt to read a number from a json value; log warning on failure.
static std::optional<double>
require_number(const boost::json::value &v, std::string_view field,
               std::string_view context) {
  if (v.is_number()) {
    return v.to_number<double>();
  }
  THALAMUS_LOG(warning) << "[ZedNode] Field '" << field << "' in " << context
                        << " is not a number (got kind="
                        << static_cast<int>(v.kind()) << ")";
  return std::nullopt;
}

/// Attempt to read a string from a json value; log warning on failure.
static std::optional<std::string>
require_string(const boost::json::value &v, std::string_view field,
               std::string_view context) {
  if (auto *s = v.if_string()) {
    return std::string(*s);
  }
  THALAMUS_LOG(warning) << "[ZedNode] Field '" << field << "' in " << context
                        << " is not a string";
  return std::nullopt;
}

/// Attempt to read an array from a json value; log warning on failure.
static const boost::json::array *
require_array(const boost::json::value &v, std::string_view field,
              std::string_view context) {
  if (auto *a = v.if_array()) {
    return a;
  }
  THALAMUS_LOG(warning) << "[ZedNode] Field '" << field << "' in " << context
                        << " is not an array";
  return nullptr;
}

/// Attempt to read an object from a json value; log warning on failure.
static const boost::json::object *
require_object(const boost::json::value &v, std::string_view field,
               std::string_view context) {
  if (auto *o = v.if_object()) {
    return o;
  }
  THALAMUS_LOG(warning) << "[ZedNode] Field '" << field << "' in " << context
                        << " is not an object";
  return nullptr;
}

/// Check an object contains a key, log warning if not.
static const boost::json::value *
require_field(const boost::json::object &obj, std::string_view key,
              std::string_view context) {
  auto it = obj.find(key);
  if (it == obj.end()) {
    THALAMUS_LOG(warning) << "[ZedNode] Missing required field '" << key
                          << "' in " << context;
    return nullptr;
  }
  return &it->value();
}

/// Parse a 3-element JSON array into a float[3].
/// Returns false and logs a warning on any failure.
static bool parse_vec3(const boost::json::array &arr, std::string_view context,
                       float (&out)[3]) {
  if (arr.size() < 3) {
    THALAMUS_LOG(warning) << "[ZedNode] Expected 3-element array in "
                          << context << " but got " << arr.size();
    return false;
  }
  auto x = require_number(arr[0], "x", context);
  auto y = require_number(arr[1], "y", context);
  auto z = require_number(arr[2], "z", context);
  if (!x || !y || !z) return false;
  out[0] = float(*x);
  out[1] = float(*y);
  out[2] = float(*z);
  return true;
}

/// Parse a 4-element JSON array [x,y,z,w] into a float[4] stored as [w,x,y,z].
/// Returns false and logs a warning on any failure.
static bool parse_quat_xyzw(const boost::json::array &arr,
                             std::string_view context,
                             float (&out)[4]) {
  if (arr.size() < 4) {
    THALAMUS_LOG(warning) << "[ZedNode] Expected 4-element array in "
                          << context << " but got " << arr.size();
    return false;
  }
  auto x = require_number(arr[0], "x", context);
  auto y = require_number(arr[1], "y", context);
  auto z = require_number(arr[2], "z", context);
  auto w = require_number(arr[3], "w", context);
  if (!x || !y || !z || !w) return false;
  // ZED: [x,y,z,w] → store as [w,x,y,z]
  out[0] = float(*w);
  out[1] = float(*x);
  out[2] = float(*y);
  out[3] = float(*z);
  return true;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct ZedNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  boost::asio::ip::udp::socket socket;
  ZedNode *outer;

  // Receive buffer — 1 MB, matching the Python receiver
  std::vector<unsigned char> buffer;

  // Node state
  bool is_running = false;
  size_t port = 5005;
  std::chrono::nanoseconds time;
  std::chrono::nanoseconds frame_interval = 0ns;
  long long actor = 0; // which body ID to track (0 = first seen)

  // Segment storage
  thalamus::vector<MotionCaptureNode::Segment> _segments;
  std::span<MotionCaptureNode::Segment const> _segment_span;

  // Camera pose in world frame (from payload)
  float camera_position[3] = {0, 0, 0};
  float camera_orientation[4] = {1, 0, 0, 0}; // [w,x,y,z]
  bool has_camera_pose = false;

  // Coordinate system and body format strings received from streamer
  std::string coordinate_system;
  std::string last_body_format; // changes made by pb — cached to avoid per-frame log spam

  bool _has_analog_data = false;
  bool _has_motion_data = false;

  // Analog channels: one x/y/z triple per joint, sized dynamically.
  thalamus::vector<std::string> _channel_names;
  thalamus::vector<double> _analog_flat;
  size_t _num_joints_setup = 0; // how many joints the current channel arrays cover

  // Timing
  std::chrono::nanoseconds last_timestamp = 0ns;
  unsigned int frame_count = 0;

  Impl(ObservableDictPtr _state, boost::asio::io_context &io_context,
       ZedNode *_outer)
      : state(_state),
        socket(io_context),
        outer(_outer),
        buffer(1024 * 1024) {
    using namespace std::placeholders;
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    // Channels are set up dynamically when the first body frame arrives.
  }

  // Rebuild channel names and data array for a new joint count.
  // Safe to call whenever the body format changes (e.g. BODY18 → BODY34).
  void setup_analog_channels(size_t num_joints) {
    if (num_joints == _num_joints_setup) return;
    _num_joints_setup = num_joints;
    _channel_names.clear();
    for (size_t i = 0; i < num_joints; ++i) {
      auto jn = joint_name(i, num_joints);
      _channel_names.push_back(jn + "_x");
      _channel_names.push_back(jn + "_y");
      _channel_names.push_back(jn + "_z");
    }
    _analog_flat.assign(num_joints * 3, 0.0);
  }

  ~Impl() {
    (*state)["Running"].assign(false, [&] {});
  }

  // -------------------------------------------------------------------------
  // Parse one UDP datagram
  // -------------------------------------------------------------------------
  void on_receive(const boost::system::error_code &error, size_t length) {
    TRACE_EVENT("thalamus", "ZedNode::on_receive");

    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    if (error) {
      THALAMUS_LOG(error) << "[ZedNode] UDP receive error: " << error.message();
      return;
    }

    time = std::chrono::steady_clock::now().time_since_epoch();

    // ---- Parse JSON ----------------------------------------------------------
    boost::json::value root;
    try {
      std::string_view sv(reinterpret_cast<char *>(buffer.data()), length);
      root = boost::json::parse(sv);
    } catch (const std::exception &e) {
      THALAMUS_LOG(warning) << "[ZedNode] Failed to parse JSON datagram: "
                            << e.what();
      rearm();
      return;
    }

    const auto *top = root.if_object();
    if (!top) {
      THALAMUS_LOG(warning) << "[ZedNode] Top-level JSON is not an object";
      rearm();
      return;
    }

    // ---- Required top-level fields -------------------------------------------
    const auto *ts_val = require_field(*top, "timestamp_ms", "root");
    if (!ts_val) { rearm(); return; }
    auto ts_opt = require_number(*ts_val, "timestamp_ms", "root");
    if (!ts_opt) { rearm(); return; }
    auto timestamp_ms = static_cast<long long>(*ts_opt);

    // Frame interval estimation
    auto timestamp_ns = std::chrono::milliseconds(timestamp_ms);
    if (last_timestamp != 0ns) {
      frame_interval = timestamp_ns - last_timestamp;
    }
    last_timestamp = timestamp_ns;

    // ---- Coordinate system (warn if absent) ----------------------------------
    if (auto *cs_val = top->if_contains("coordinate_system")) {
      if (auto s = require_string(*cs_val, "coordinate_system", "root")) {
        if (coordinate_system != *s) {
          coordinate_system = *s;
          THALAMUS_LOG(info) << "[ZedNode] Coordinate system: "
                             << coordinate_system;
        }
      }
    } else {
      THALAMUS_LOG(warning)
          << "[ZedNode] 'coordinate_system' not present in payload. "
             "XSens/ZED alignment may be ambiguous.";
    }

    // ---- is_world_frame (warn if absent or false) ----------------------------
    bool is_world_frame = false;
    if (auto *wf_val = top->if_contains("is_world_frame")) {
      if (wf_val->is_bool()) {
        is_world_frame = wf_val->as_bool();
        if (!is_world_frame) {
          THALAMUS_LOG(warning)
              << "[ZedNode] 'is_world_frame' is false — positions are "
                 "camera-relative. XSens integration will not be possible "
                 "without positional tracking enabled on the ZED camera.";
        }
      } else {
        THALAMUS_LOG(warning)
            << "[ZedNode] 'is_world_frame' is not a boolean";
      }
    } else {
      THALAMUS_LOG(warning)
          << "[ZedNode] 'is_world_frame' not present in payload.";
    }

    // ---- Camera pose (warn if absent) ----------------------------------------
    has_camera_pose = false;
    if (auto *cp_val = top->if_contains("camera_pose")) {
      if (const auto *cp_obj = require_object(*cp_val, "camera_pose", "root")) {
        bool pos_ok = false, ori_ok = false;

        if (auto *pos_val = cp_obj->if_contains("position")) {
          if (const auto *pos_arr = require_array(*pos_val, "camera_pose.position", "camera_pose")) {
            pos_ok = parse_vec3(*pos_arr, "camera_pose.position", camera_position);
          }
        } else {
          THALAMUS_LOG(warning) << "[ZedNode] Missing 'position' in camera_pose";
        }

        if (auto *ori_val = cp_obj->if_contains("orientation")) {
          if (const auto *ori_arr = require_array(*ori_val, "camera_pose.orientation", "camera_pose")) {
            ori_ok = parse_quat_xyzw(*ori_arr, "camera_pose.orientation", camera_orientation);
          }
        } else {
          THALAMUS_LOG(warning) << "[ZedNode] Missing 'orientation' in camera_pose";
        }

        has_camera_pose = pos_ok && ori_ok;
        if (!has_camera_pose) {
          THALAMUS_LOG(warning)
              << "[ZedNode] camera_pose partially or fully invalid. "
                 "XSens/ZED coordinate alignment will not be possible.";
        }
      }
    } else {
      THALAMUS_LOG(warning)
          << "[ZedNode] 'camera_pose' not present in payload. "
             "XSens/ZED coordinate alignment will not be possible.";
    }

    // ---- Bodies --------------------------------------------------------------
    const auto *bodies_val = require_field(*top, "bodies", "root");
    if (!bodies_val) { rearm(); return; }
    const auto *bodies_arr = require_array(*bodies_val, "bodies", "root");
    if (!bodies_arr) { rearm(); return; }

    if (bodies_arr->empty()) {
      // Not a warning — zero bodies is a valid frame
      _has_motion_data = false;
      _has_analog_data = false;
      outer->ready(outer);
      rearm();
      return;
    }

    // Log body format once when it changes
    if (auto *bf_val = top->if_contains("body_format")) {
      if (auto s = require_string(*bf_val, "body_format", "root")) {
        if (last_body_format != *s) {
          last_body_format = *s;
          THALAMUS_LOG(info) << "[ZedNode] body_format=" << last_body_format;
        }
      }
    }

    // ---- Build segments for every tracked body --------------------------------
    _segments.clear();
    bool any_parse_failure = false;
    thalamus::vector<long long> received_ids;
    size_t first_body_num_joints = 0; // joint count of the first tracked body

    for (auto &bv : *bodies_arr) {
      const auto *bo = bv.if_object();
      if (!bo) {
        THALAMUS_LOG(warning) << "[ZedNode] Body entry is not a JSON object";
        continue;
      }

      // Skip non-tracked bodies
      if (auto *ts_field = bo->if_contains("tracking_state")) {
        auto ts_str = require_string(*ts_field, "tracking_state", "body");
        if (ts_str && *ts_str != "Ok") continue;
      }

      long long body_id = -1;
      if (auto *id_val = bo->if_contains("id")) {
        if (auto id_opt = require_number(*id_val, "id", "body"))
          body_id = static_cast<long long>(*id_opt);
      }

      // Prefer keypoints_3d for positions (always populated by ZED streamer).
      // Fall back to local_position_per_joint if keypoints_3d is absent/empty.
      const boost::json::array *lp_arr = nullptr;
      for (auto *field : {"keypoints_3d", "local_position_per_joint"}) {
        if (auto *v = bo->if_contains(field)) {
          if (auto *a = v->if_array(); a && !a->empty()) {
            lp_arr = a;
            break;
          }
        }
      }
      if (!lp_arr) continue;

      // Orientations: use local_orientation_per_joint if populated.
      const boost::json::array *lo_arr = nullptr;
      if (auto *v = bo->if_contains("local_orientation_per_joint")) {
        if (auto *a = v->if_array(); a && !a->empty())
          lo_arr = a;
      }
      const bool has_orientation = lo_arr && lo_arr->size() == lp_arr->size();
      if (lo_arr && !has_orientation) {
        THALAMUS_LOG(warning) << "[ZedNode] Body " << body_id
                              << ": orientation count mismatch, using identity";
      }

      const size_t num_joints = lp_arr->size();
      received_ids.push_back(body_id);

      // Use the first tracked body to size the analog channels.
      if (first_body_num_joints == 0) {
        first_body_num_joints = num_joints;
        setup_analog_channels(num_joints);
      }

      for (size_t i = 0; i < num_joints; ++i) {
        const auto jname = joint_name(i, num_joints);
        const auto *pos_arr = require_array((*lp_arr)[i], jname, "keypoints_3d");
        if (!pos_arr) {
          any_parse_failure = true;
          continue;
        }

        auto &seg = _segments.emplace_back();
        seg.segment_id = static_cast<unsigned int>(i + 1);
        seg.actor = static_cast<unsigned char>(body_id < 0 ? 0 : body_id & 0xFF);
        seg.frame = frame_count;
        seg.time = static_cast<unsigned int>(timestamp_ms & 0xFFFFFFFF);

        if (!parse_vec3(*pos_arr, jname + ".position", seg.position))
          any_parse_failure = true;

        if (has_orientation) {
          const auto *ori_arr = require_array((*lo_arr)[i], jname, "local_orientation_per_joint");
          if (!ori_arr || !parse_quat_xyzw(*ori_arr, jname + ".orientation", seg.rotation))
            any_parse_failure = true;
        } else {
          // Identity quaternion [w=1, x=0, y=0, z=0]
          seg.rotation[0] = 1.f; seg.rotation[1] = 0.f;
          seg.rotation[2] = 0.f; seg.rotation[3] = 0.f;
        }
      }
    } // end per-body loop

    if (any_parse_failure) {
      THALAMUS_LOG(warning)
          << "[ZedNode] One or more joints failed to parse in frame "
          << frame_count << ". Segment data may be incomplete.";
    }

    _segment_span =
        std::span<MotionCaptureNode::Segment const>(_segments.begin(),
                                                    _segments.end());
    _has_motion_data = !_segments.empty();

    // Populate analog channels from the first tracked body's joints.
    // first_body_num_joints is guaranteed to equal _num_joints_setup here.
    for (size_t i = 0; i < first_body_num_joints; ++i) {
      _analog_flat[i * 3 + 0] = _segments[i].position[0];
      _analog_flat[i * 3 + 1] = _segments[i].position[1];
      _analog_flat[i * 3 + 2] = _segments[i].position[2];
    }
    _has_analog_data = _has_motion_data;

    if (frame_count % 100 == 0) {
      std::string id_list;
      for (size_t k = 0; k < received_ids.size(); ++k) {
        if (k) id_list += ", ";
        id_list += std::to_string(received_ids[k]);
      }
      THALAMUS_LOG(info) << "[ZedNode] Frame " << frame_count
                         << ": bodies [" << id_list << "] ("
                         << _segments.size() << " joints total)";
    }

    outer->ready(outer);
    ++frame_count;
    rearm();
  }

  // Re-queue the async receive
  void rearm() {
    socket.async_receive(
        boost::asio::buffer(buffer.data(), buffer.size()),
        std::bind(&Impl::on_receive, this,
                  std::placeholders::_1, std::placeholders::_2));
  }

  // -------------------------------------------------------------------------
  // State change handler
  // -------------------------------------------------------------------------
  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &key,
                 const ObservableCollection::Value &value) {
    TRACE_EVENT("thalamus", "ZedNode::on_change");
    auto key_str = std::get<std::string>(key);

    if (key_str == "Actor") {
      actor = std::get<long long>(value);
      return;
    }

    if (!state->contains("Running")) {
      return;
    }

    auto old_is_running = is_running;
    is_running = state->at("Running");

    if (!is_running) {
      if (old_is_running && socket.is_open()) {
        socket.close();
      }
      return;
    }

    if (!state->contains("Port")) {
      THALAMUS_LOG(warning) << "[ZedNode] 'Port' not set in node state.";
      return;
    }
    port = state->at("Port");

    if (old_is_running == is_running) {
      return;
    }

    boost::system::error_code error;
    socket.open(boost::asio::ip::udp::v4(), error);
    if (error) {
      THALAMUS_LOG(error) << "[ZedNode] Failed to open socket: "
                          << error.message();
      return;
    }
    socket.bind(
        boost::asio::ip::udp::endpoint(
            boost::asio::ip::make_address("0.0.0.0"), uint16_t(port)),
        error);
    if (error) {
      THALAMUS_LOG(error) << "[ZedNode] Failed to bind UDP port " << port
                          << ": " << error.message();
      return;
    }

    THALAMUS_LOG(info) << "[ZedNode] Listening on UDP port " << port;
    frame_count = 0;
    last_timestamp = 0ns;
    frame_interval = 0ns;
    rearm();
  }
};

// ---------------------------------------------------------------------------
// ZedNode public API
// ---------------------------------------------------------------------------

ZedNode::ZedNode(ObservableDictPtr state,
                 boost::asio::io_context &io_context,
                 NodeGraph *graph)
    : impl(new Impl(state, io_context, this)) {
  (void)graph;
}

ZedNode::~ZedNode() {}

std::string ZedNode::type_name() { return "ZED"; }

std::span<MotionCaptureNode::Segment const> ZedNode::segments() const {
  return impl->_segment_span;
}

const std::string_view ZedNode::pose_name() const {
  // ZED BODY-18 does not provide pose classification
  return "";
}

void ZedNode::inject(const std::span<Segment const> &segments) {
  impl->time = std::chrono::steady_clock::now().time_since_epoch();
  impl->_segment_span = segments;
  impl->_has_analog_data = false;
  impl->_has_motion_data = true;
  ready(this);
}

std::chrono::nanoseconds ZedNode::time() const { return impl->time; }

std::chrono::nanoseconds ZedNode::sample_interval(int) const {
  return impl->frame_interval;
}

std::span<const double> ZedNode::data(int channel) const {
  return std::span<const double>(&impl->_analog_flat[size_t(channel)], 1);
}

int ZedNode::num_channels() const {
  return int(impl->_channel_names.size());
}

std::string_view ZedNode::name(int channel) const {
  return impl->_channel_names[channel];
}

bool ZedNode::has_analog_data() const { return impl->_has_analog_data; }
bool ZedNode::has_motion_data() const { return impl->_has_motion_data; }

void ZedNode::inject(const thalamus::vector<std::span<double const>> &spans,
                     const thalamus::vector<std::chrono::nanoseconds> &,
                     const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(spans.size() == 1, "Bad dims");
  THALAMUS_ASSERT(spans.front().size() == 1, "Bad dims");
}

size_t ZedNode::modalities() const { return infer_modalities<ZedNode>(); }

} // namespace thalamus
