#include <xsens_node.h>
#include <vector>
#include <map>
#include <functional>
#include <util.h>
#include <tracing/tracing.h>
#include <boost/json.hpp>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/vec_operations.hpp>
#include <boost/qvm/quat_access.hpp>
#include <boost/qvm/quat_operations.hpp>
#include <boost/qvm/quat_vec_operations.hpp>
#include <boost/qvm/swizzle.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <modalities_util.h>

namespace thalamus {
  class RateTracker {
    std::vector<std::chrono::nanoseconds> heap;
    std::chrono::nanoseconds last_now;
    const std::optional<double> rate_limit;
    std::chrono::nanoseconds window;
    std::function<bool(const std::chrono::nanoseconds&, const std::chrono::nanoseconds&)> order = [](auto a, auto b) { return a > b; };
  public:
    RateTracker(std::chrono::nanoseconds window, double rate_limit = std::numeric_limits<double>::max())
      : rate_limit(rate_limit)
      , window(window) {}

    bool update(std::chrono::nanoseconds now) {
      while(!heap.empty() && now - heap.front() > window) {
        std::pop_heap(heap.begin(), heap.end(), order);
        heap.pop_back();
      }

      if(new_rate(now) > rate_limit) {
        return false;
      }

      last_now = now;
      heap.push_back(now);
      std::push_heap(heap.begin(), heap.end(), order);
      return true;
    }
    double rate() const {
      if(heap.size() < 2) {
        return 0;
      }
      double ticks = (last_now - heap.front()).count();
      auto seconds = ticks/std::chrono::nanoseconds::period::den;
      return (heap.size()-1)/seconds;
    }
    double new_rate(std::chrono::nanoseconds now) const {
      if(heap.size() < 1) {
        return 0;
      }
      double ticks = (now - heap.front()).count();
      auto seconds = ticks/std::chrono::nanoseconds::period::den;
      return heap.size()/seconds;
    }

    std::chrono::nanoseconds duration() const {
      if(heap.empty()) {
        return 0s;
      }
      return last_now - heap.front();
    }
  };

  static std::string POSE_CHANGE("Pose Change");
  static std::string LEFT_THUMB_DISTANCE("Left Thumb Distance (m)");
  static std::string LEFT_INDEX_DISTANCE("Left Index Distance (m)");
  static std::string LEFT_MIDDLE_DISTANCE("Left Middle Distance (m)");
  static std::string LEFT_RING_DISTANCE("Left Ring Distance (m)");
  static std::string LEFT_PINKY_DISTANCE("Left Pinky Distance (m)");
  static std::string RIGHT_THUMB_DISTANCE("Right Thumb Distance (m)");
  static std::string RIGHT_INDEX_DISTANCE("Right Index Distance (m)");
  static std::string RIGHT_MIDDLE_DISTANCE("Right Middle Distance (m)");
  static std::string RIGHT_RING_DISTANCE("Right Ring Distance (m)");
  static std::string RIGHT_PINKY_DISTANCE("Right Pinky Distance (m)");

  static thalamus::vector<std::string> BASE_XSENS_CHANNEL_NAMES{
    POSE_CHANGE,
    LEFT_THUMB_DISTANCE, LEFT_INDEX_DISTANCE, LEFT_MIDDLE_DISTANCE, LEFT_RING_DISTANCE, LEFT_PINKY_DISTANCE,
    RIGHT_THUMB_DISTANCE, RIGHT_INDEX_DISTANCE, RIGHT_MIDDLE_DISTANCE, RIGHT_RING_DISTANCE, RIGHT_PINKY_DISTANCE};

  struct XsensNode::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::asio::high_resolution_timer timer;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::socket remote_control_socket;
    int address[6];
    std::vector<Segment> _segments;
    std::span<Segment const> _segment_span;
    bool is_running = false;
    unsigned char buffer[4096];
    size_t port;
    XsensNode* outer;
    std::chrono::nanoseconds time;
    std::string pose_name;
    double pose_change;
    struct Finger {
      double min = std::numeric_limits<double>::max();
      double max = -std::numeric_limits<double>::max();
      double value;
      double up() {
        return (value-min)/(max-min);
      }
      void update(double value) {
        min = std::min(min, value);
        max = std::max(max, value);
        this->value = value;
      }
      void reset() {
        min = std::numeric_limits<double>::max();
        max = -std::numeric_limits<double>::max();
      }
    };
    std::vector<double> pose_distances;
    std::array<Finger, 10> fingers;
    std::chrono::nanoseconds frame_interval = 0ns;
    RateTracker rate_tracker;
    bool has_analog_data = false;
    std::chrono::nanoseconds start_time = 0ns;
    std::vector<std::string> channel_names;

    enum class SendType {
      Current,
      Min,
      Max
    };
    SendType send_type = SendType::Current;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, XsensNode* outer)
      : state(state)
      , timer(io_context)
      , socket(io_context)
      , remote_control_socket(io_context)
      , outer(outer)
      , rate_tracker(1s)
      , channel_names(BASE_XSENS_CHANNEL_NAMES.begin(), BASE_XSENS_CHANNEL_NAMES.end()) {

      if(std::filesystem::exists(std::filesystem::path(".xsens_cache"))) {
        std::ifstream input(".xsens_cache", std::ios::in | std::ios::binary);
        input.read(reinterpret_cast<char*>(fingers.data()), sizeof(Finger)*fingers.size());
        print_fingers();
      }
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      memset(&address, 0, sizeof(address));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    void print_fingers() {
      auto i = 0;
      for(auto& finger : fingers) {
        THALAMUS_LOG(info) << ++i << " " << finger.min << " " << finger.value << " " << finger.max;
      }
    }

    ~Impl() {
      (*state)["Running"].assign(false, [&] {});
    }

    void on_receive(const boost::system::error_code& error, size_t) {
      TRACE_EVENT0("thalamus", "XsensNode::on_receive");
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      if(error) {
        THALAMUS_LOG(error) <<  "Receiving Xsens message failed " << error.message();
        return;
      }
      time = std::chrono::steady_clock::now().time_since_epoch();
      if(start_time == 0ns) {
        start_time = time;
      }

      rate_tracker.update(time);
      if(frame_interval == 0ns && time - start_time > 2s) {
        frame_interval = std::chrono::nanoseconds(size_t(1e9/rate_tracker.rate()));
      }

      std::string id_string(reinterpret_cast<char*>(buffer), 6);
      if(id_string != "MXTP02") {
        socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::on_receive, this, _1, _2));
        return;
      }

      unsigned int sample_counter = ntohl(*reinterpret_cast<unsigned int*>(buffer + 6));
      //unsigned char datagram_counter = *reinterpret_cast<unsigned char*>(buffer + 10);
      //unsigned char number_of_items = *reinterpret_cast<unsigned char*>(buffer + 11);
      unsigned int time_code = ntohl(*reinterpret_cast<unsigned int*>(buffer + 12));
      //unsigned char character_id = *reinterpret_cast<unsigned char*>(buffer + 16);
      //unsigned char num_body_segments = *reinterpret_cast<unsigned char*>(buffer + 17);
      //unsigned char num_props = *reinterpret_cast<unsigned char*>(buffer + 18);
      //unsigned char num_finger_segments = *reinterpret_cast<unsigned char*>(buffer + 19);
      unsigned short payload_size = ntohs(*reinterpret_cast<unsigned short*>(buffer + 22));

      _segments.clear();
      unsigned char* position = buffer + 24;
      unsigned char* end = position + payload_size;
      while (position < end) {
        _segments.push_back(Segment::parse(position));
        _segments.back().frame = sample_counter;
        _segments.back().time = time_code;
        position += Segment::serialized_size;
      }
      _segment_span = std::span<Segment const>(_segments.begin(), _segments.end());

      if(frame_interval != 0ns) {
        auto hand_offset = 23;
        fingers[0].update(boost::qvm::mag(_segments[hand_offset + 3].position - _segments[hand_offset + 2].position));
        fingers[1].update(boost::qvm::mag(_segments[hand_offset + 7].position - _segments[hand_offset + 5].position));
        fingers[2].update(boost::qvm::mag(_segments[hand_offset + 11].position - _segments[hand_offset + 9].position));
        fingers[3].update(boost::qvm::mag(_segments[hand_offset + 15].position - _segments[hand_offset + 13].position));
        fingers[4].update(boost::qvm::mag(_segments[hand_offset + 19].position - _segments[hand_offset + 17].position));
        hand_offset = 43;
        fingers[5].update(boost::qvm::mag(_segments[hand_offset + 3].position - _segments[hand_offset + 2].position));
        fingers[6].update(boost::qvm::mag(_segments[hand_offset + 7].position - _segments[hand_offset + 5].position));
        fingers[7].update(boost::qvm::mag(_segments[hand_offset + 11].position - _segments[hand_offset + 9].position));
        fingers[8].update(boost::qvm::mag(_segments[hand_offset + 15].position - _segments[hand_offset + 13].position));
        fingers[9].update(boost::qvm::mag(_segments[hand_offset + 19].position - _segments[hand_offset + 17].position));
        has_analog_data = true;

        std::vector<double> mask(5, 0);
        auto offset = pose_with_left_hand ? 0 : 5;
        //i = 0 so we can skip thumb which never moves according to the current metric.
        for(auto i = 1;i < 5;++i) {
          mask[mask.size()-i-1] = fingers[offset + i].up();
        }

        pose_distances.resize(poses->size());
        channel_names.resize(BASE_XSENS_CHANNEL_NAMES.size() + poses->size());
        auto last_pose = pose_name;
        pose_name = "";
        auto min_distance = std::numeric_limits<double>::max();
        for(size_t i = 0;i < poses->size();++i) {
          ObservableListPtr pose = poses->at(i);
          long long pose_mask = pose->at(0);
          auto distance = 0.0;
          for(auto d : mask) {
            distance += std::abs(d - (pose_mask & 0x01));
            pose_mask >>= 1;
          }
          distance /= mask.size();
          pose_distances[i] = 1-distance;
          channel_names[BASE_XSENS_CHANNEL_NAMES.size() + i] = static_cast<std::string>(pose->at(1));

          if(distance < .5 && distance < min_distance) {
            pose_name = channel_names[BASE_XSENS_CHANNEL_NAMES.size() + i];
            min_distance = distance;
          }
        }
        pose_change = last_pose == pose_name ? 0 : 5;
      } else {
        has_analog_data = false;
      }

      outer->ready(outer);

      socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::on_receive, this, _1, _2));
    }

    ObservableListPtr poses = std::make_shared<ObservableList>();
    bool pose_with_left_hand = false;
    boost::asio::ip::address xsens_address;
    size_t xsens_port;
    boost::asio::ip::udp::endpoint xsens_endpoint;

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& key, const ObservableCollection::Value& value) {
      auto key_str = std::get<std::string>(key);
      if(key_str == "Poses") {
        poses = std::get<ObservableListPtr>(value);
        return;
      } else if (key_str == "Pose Hand") {
        pose_with_left_hand = std::get<std::string>(value) == "Left";
        return;
      } else if (key_str == "Send Type") {
        auto key_str = std::get<std::string>(value);
        if(key_str == "Current") {
          send_type = SendType::Current;
        } else if (key_str == "Max") {
          send_type = SendType::Max;
        } else if (key_str == "Min") {
          send_type = SendType::Min;
        }
        return;
      } else if (key_str == "Xsens Address") {
        auto value_str = std::get<std::string>(value);
        std::vector<std::string> tokens = absl::StrSplit(value_str, ':');
        if(tokens.size() != 2) {
          (*state)["Xsens Address Good"].assign(false);
          return;
        }
        boost::system::error_code error;
        auto address_text = tokens.at(0);
        if(address_text == "localhost") {
          address_text = "127.0.0.1";
        }
        xsens_address = boost::asio::ip::make_address(address_text, error);
        if(error) {
          (*state)["Xsens Address Good"].assign(false);
          return;
        }
        auto success = absl::SimpleAtoi(tokens.at(1), &xsens_port);
        if(!success) {
          (*state)["Xsens Address Good"].assign(false);
          return;
        }
        (*state)["Xsens Address Good"].assign(true);
        xsens_endpoint = boost::asio::ip::udp::endpoint(xsens_address, xsens_port);
        return;
      }

      if (!state->contains("Running")) {
        return;
      }
      auto old_is_running = is_running;
      is_running = state->at("Running");
      if (!is_running) {
        if (old_is_running) {
          xsens_command = "<StopRecordingReq/>";
          boost::system::error_code error;
          socket.send_to(boost::asio::const_buffer(xsens_command.data(), xsens_command.size()), xsens_endpoint, 0, error);
          if (error) {
            THALAMUS_LOG(warning) << "Xsens Remote Stop Failed: " << error.message();
          }
          socket.close();
        }
        return;
      }

      if (!state->contains("Port")) {
        return;
      }
      port = state->at("Port");

      if (old_is_running == is_running) {
        return;
      }

      socket.open(boost::asio::ip::udp::v4());
      boost::system::error_code error;
      socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::make_address("0.0.0.0"), port), error);
      THALAMUS_ASSERT(!error, "%s", error.message());
      start_time = 0ns;
      frame_interval = 0ns;
      socket.async_receive(boost::asio::buffer(buffer, sizeof(buffer)), std::bind(&Impl::on_receive, this, _1, _2));

      const auto start_time = absl::FromChrono(std::chrono::system_clock::now());
      auto start_time_str = absl::FormatTime("%Y%m%d%H%M%S", start_time, absl::LocalTimeZone());
      xsens_command = "<StartRecordingReq SessionName=\"Thalamus_" + start_time_str + "\"/>";
      socket.send_to(boost::asio::const_buffer(xsens_command.data(), xsens_command.size()), xsens_endpoint, 0, error);
      if(error) {
        THALAMUS_LOG(warning) << "Xsens Remote Start Failed: " << error.message();
      }
    }

    std::string xsens_command;
  };

  XsensNode::XsensNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*)
    : impl(new Impl(state, io_context, this)) {}

  XsensNode::~XsensNode() {}

  std::string XsensNode::type_name() {
    return "XSENS";
  }

  std::span<XsensNode::Segment const> XsensNode::segments() const {
    return impl->_segment_span;
  }
  const std::string& XsensNode::pose_name() const {
    return impl->pose_name;
  }

  void XsensNode::inject(const std::span<Segment const>& segments) {
    impl->_segment_span = segments;
    ready(this);
  }

  const size_t MotionCaptureNode::Segment::serialized_size = 32;

  XsensNode::Segment XsensNode::Segment::parse(unsigned char* data) {
    Segment segment;
    segment.segment_id = ntohl(*reinterpret_cast<unsigned int*>(data));

    unsigned int temp;

    temp = ntohl(*reinterpret_cast<unsigned int*>(data + 4));
    boost::qvm::X(segment.position) = *reinterpret_cast<float*>(&temp);
    temp = ntohl(*reinterpret_cast<unsigned int*>(data + 8));
    boost::qvm::Y(segment.position) = *reinterpret_cast<float*>(&temp);
    temp = ntohl(*reinterpret_cast<unsigned int*>(data + 12));
    boost::qvm::Z(segment.position) = *reinterpret_cast<float*>(&temp);

    temp = ntohl(*reinterpret_cast<unsigned int*>(data + 16));
    boost::qvm::S(segment.rotation) = *reinterpret_cast<float*>(&temp);
    temp = ntohl(*reinterpret_cast<unsigned int*>(data + 20));
    boost::qvm::X(segment.rotation) = *reinterpret_cast<float*>(&temp);
    temp = ntohl(*reinterpret_cast<unsigned int*>(data + 24));
    boost::qvm::Y(segment.rotation) = *reinterpret_cast<float*>(&temp);
    temp = ntohl(*reinterpret_cast<unsigned int*>(data + 28));
    boost::qvm::Z(segment.rotation) = *reinterpret_cast<float*>(&temp);

    return segment;
  }

  std::chrono::nanoseconds XsensNode::time() const {
    return impl->time;
  }

  std::span<const double> XsensNode::data(int channel) const {
    if(channel < 1) {
      return std::span<const double>(&impl->pose_change, &impl->pose_change + 1);
    } else if(channel < 11) {
      switch(impl->send_type) {
      case Impl::SendType::Current:
        return std::span<const double>(&impl->fingers[channel-1].value, &impl->fingers[channel-1].value + 1);
      case Impl::SendType::Max:
        return std::span<const double>(&impl->fingers[channel-1].max, &impl->fingers[channel-1].max + 1);
      case Impl::SendType::Min:
        return std::span<const double>(&impl->fingers[channel-1].min, &impl->fingers[channel-1].min + 1);
      }
    } else {
      return std::span<const double>(&impl->pose_distances[channel-11], &impl->pose_distances[channel-11] + 1);
    }
    THALAMUS_ASSERT(false, "Unexpected channel: %d", channel);
  }

  int XsensNode::num_channels() const {
    return impl->channel_names.size();
  }

  std::string_view XsensNode::name(int channel) const {
    return impl->channel_names.at(channel);
  }
  std::span<const std::string> XsensNode::get_recommended_channels() const {
    return std::span<const std::string>(impl->channel_names.begin(), impl->channel_names.end());
  }

  std::chrono::nanoseconds XsensNode::sample_interval(int) const {
    return impl->frame_interval;
  }

  void XsensNode::inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
    THALAMUS_ASSERT(spans.size() == 1);
    THALAMUS_ASSERT(spans.front().size() == 1);
  }
   
  bool XsensNode::has_analog_data() const {
    return impl->has_analog_data;
  }

  bool XsensNode::has_motion_data() const {
    return true;
  }

  boost::json::value XsensNode::process(const boost::json::value& value) {
    auto text = value.as_string();
    if(text == "Cache") {
      std::ofstream output(".xsens_cache", std::ios::out | std::ios::binary | std::ios::ate);
      output.write(reinterpret_cast<char*>(impl->fingers.data()), sizeof(Impl::Finger)*impl->fingers.size());
      impl->print_fingers();
    } else if (text == "Reset") {
      for(auto& finger : impl->fingers) {
        finger.reset();
      }
    }
    return boost::json::value();
  }

  static std::map<std::string, unsigned int, std::less<>> HAND_ENGINE_TO_XSENS_SEGMENT_IDS;

  static std::once_flag hand_engine_setup_flag;

  struct HandEngineNode::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::asio::io_context& io_context;
    boost::asio::high_resolution_timer timer;
    boost::asio::ip::tcp::socket socket;
    std::chrono::nanoseconds time;
    size_t buffer_size;
    std::vector<short> short_buffer;
    std::vector<double> double_buffer;
    std::vector<int> channels;
    std::map<size_t, std::function<void(Node*)>> observers;
    //double sample_rate;
    size_t counter = 0;
    int address[6];
    std::string buffer;
    std::string pose_name;
    size_t buffer_offset;
    unsigned int message_size;
    unsigned int num_props = 0;
    bool is_running = false;
    bool has_analog_data = false;
    bool has_motion_data = false;
    unsigned int frame_count;
    thalamus::vector<MotionCaptureNode::Segment> _segments;
    std::span<Segment const> _segment_span;
    HandEngineNode* outer;
    std::string address_str;
    bool is_connected = false;
    double amplitude;
    double value;
    std::chrono::milliseconds duration;
  public:
    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*, HandEngineNode* outer)
      : state(state)
      , io_context(io_context)
      , timer(io_context)
      , socket(io_context)
      , outer(outer)
      , amplitude(5)
      , duration(16) {
      std::call_once(hand_engine_setup_flag, [&] {
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "hand_l", 23 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "thumb_01_l", 24 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "thumb_02_l", 25 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "thumb_03_l", 26 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "index_00_l", 27 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "index_01_l", 28 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "index_02_l", 29 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "index_03_l", 30 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "middle_00_l", 31 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "middle_01_l", 32 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "middle_02_l", 33 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "middle_03_l", 34 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "ring_00_l", 35 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "ring_01_l", 36 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "ring_02_l", 37 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "ring_03_l", 38 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "pinky_00_l", 39 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "pinky_01_l", 40 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "pinky_02_l", 41 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "pinky_03_l", 42 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "hand_r", 43 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "thumb_01_r", 44 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "thumb_02_r", 45 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "thumb_03_r", 46 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "index_00_r", 47 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "index_01_r", 48 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "index_02_r", 49 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "index_03_r", 50 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "middle_00_r", 51 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "middle_01_r", 52 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "middle_02_r", 53 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "middle_03_r", 54 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "ring_00_r", 55 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "ring_01_r", 56 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "ring_02_r", 57 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "ring_03_r", 58 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "pinky_00_r", 59 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "pinky_01_r", 60 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "pinky_02_r", 61 });
        HAND_ENGINE_TO_XSENS_SEGMENT_IDS.insert({ "pinky_03_r", 62 });
      });

      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      memset(&address, 0, sizeof(address));
      state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [] {});
    }

    std::chrono::nanoseconds frame_interval;
    double pose_change;
    double thumb_distance;
    double index_distance;
    double middle_distance;
    double ring_distance;
    double pinky_distance;

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Running") {
        auto old_is_running = is_running;
        is_running = std::get<bool>(v);
        if (!old_is_running && is_running) {
          std::string address_str = state->at("Address");
          this->address_str = address_str;
          std::vector<std::string> address_tokens = absl::StrSplit(address_str, ':');
          if (address_tokens.size() < 2) {
            address_tokens.push_back("9000");
          }
          boost::asio::ip::tcp::resolver resolver(io_context);
          auto endpoints = resolver.resolve(address_tokens.at(0), address_tokens.at(1));
          boost::system::error_code ec;
          boost::asio::connect(socket, endpoints, ec);
          if(ec) {
            THALAMUS_LOG(warning) << "Failed to connect to " << address_str;
            (*state)["Running"].assign(false, [] {});
            return;
          }
          THALAMUS_LOG(info) << "Connected to " << address_str;
          is_connected = true;
          frame_count = 0;
          buffer_offset = 0;
          message_size = 0;
          buffer.resize(4);
          socket.async_receive(boost::asio::buffer(buffer.data(), buffer.size()), std::bind(&Impl::on_receive, this, _1, _2));
        } else if(old_is_running && !is_running && is_connected) {
          boost::system::error_code ec;
          socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
          THALAMUS_ASSERT(!ec, "%s", ec.what());
          socket.close(ec);
          THALAMUS_ASSERT(!ec, "%s", ec.what());
        }
      } else if (key_str == "Num Props") {
        num_props = 0;//std::get<long long int>(k);
      } else if (key_str == "Amplitude") {
        amplitude = std::get<double>(v);
      } else if (key_str == "Duration (ms)") {
        duration = std::chrono::milliseconds(std::get<long long int>(v));
      }
    }

    void update_pose_name(const boost::json::string& new_pose_name) {
      if(pose_name == new_pose_name) {
        return;
      }
      pose_name = new_pose_name;

      value = amplitude;
      has_analog_data = true;
      outer->ready(outer);
      has_analog_data = false;
      this->timer.expires_after(duration);
      this->timer.async_wait([this] (const boost::system::error_code& error) {
        TRACE_EVENT0("thalamus", "on_event(down)");
        if (error) {
          THALAMUS_LOG(info) << "update_pose_name " << error.message();
        }
        value = 0;
        has_analog_data = true;
        outer->ready(outer);
        has_analog_data = false;
      });
    }

    void on_receive(const boost::system::error_code& error, size_t length) {
      TRACE_EVENT0("thalamus", "XsensNode::on_receive");
      if (error.value() == boost::asio::error::operation_aborted) {
        THALAMUS_LOG(info) << "HandEngineNode disconnected from " << address_str;
        is_connected = false;
        return;
      }
      THALAMUS_ASSERT(!error, "Receiving Hand Engine message failed: %s", error.message());

      if(message_size == 0) {
        buffer_offset += length;
        if(buffer_offset == buffer.size()) {
          buffer_offset = 0;
          message_size = *reinterpret_cast<unsigned int*>(buffer.data());
          message_size = htonl(message_size);
          buffer.resize(message_size);
        }
      } else {
        buffer_offset += length;
        std::string timecode;
        if(buffer_offset == buffer.size()) {
          time = std::chrono::steady_clock::now().time_since_epoch();
          boost::json::object parsed = boost::json::parse(buffer).as_object();
          auto bones = parsed["bones"].as_array();
          auto new_pose_name = parsed["poseName"].as_string();

          pose_change = pose_name == new_pose_name ? 0 : amplitude;
          pose_name = new_pose_name;

          timecode = parsed["timecode"].as_string();
          auto frame_rate = parsed["frameRate"].to_number<unsigned int>();
          frame_interval = std::chrono::nanoseconds(size_t(decltype(frame_interval)::period::den/frame_rate));
          auto frame_ms = 1'000/frame_rate;
          thalamus::vector<std::string> timecode_tokens = absl::StrSplit(timecode, ':');
          THALAMUS_ASSERT(timecode_tokens.size() == 4, "Invalid time code = %s", timecode);
          int hour, minute, second, frame;
          auto success = absl::SimpleAtoi(timecode_tokens.at(0), &hour);
          THALAMUS_ASSERT(success, "Failed to parse hour");
          success = absl::SimpleAtoi(timecode_tokens.at(1), &minute);
          THALAMUS_ASSERT(success, "Failed to parse minute");
          success = absl::SimpleAtoi(timecode_tokens.at(2), &second);
          THALAMUS_ASSERT(success, "Failed to parse second");
          success = absl::SimpleAtoi(timecode_tokens.at(3), &frame);
          THALAMUS_ASSERT(success, "Failed to parse frame");
          auto handengine_time = frame_ms + second*1'000 + minute*60'000 + hour*3'600'000;
          _segments.clear();
          for (auto& bone_value : bones) {
            auto bone = bone_value.as_object();
            std::string_view name = bone["name"].as_string();
            auto translation = bone["translation"].as_array();

            auto pre_rotation_array = bone["pre_rotation"].as_array();
            boost::qvm::quat<float> pre_rotation{
              pre_rotation_array[3].to_number<float>(),
              pre_rotation_array[0].to_number<float>(),
              -pre_rotation_array[1].to_number<float>(),
              -pre_rotation_array[2].to_number<float>()
            };
            auto rotation_array = bone["rotation"].as_array();
            boost::qvm::quat<float> rotation{
              rotation_array[3].to_number<float>(),
              rotation_array[0].to_number<float>(),
              -rotation_array[1].to_number<float>(),
              -rotation_array[2].to_number<float>()
            };
            auto post_rotation_array = bone["post_rotation"].as_array();
            boost::qvm::quat<float> post_rotation{
              post_rotation_array[3].to_number<float>(),
              post_rotation_array[0].to_number<float>(),
              -post_rotation_array[1].to_number<float>(),
              -post_rotation_array[2].to_number<float>()
            };

            auto total_rotation = pre_rotation*rotation*post_rotation;

            auto& segment = _segments.emplace_back();
            segment.position = boost::qvm::vec<float, 3>{
              translation[0].to_number<float>(),
              -translation[1].to_number<float>(),
              -translation[2].to_number<float>()
            };
            segment.position /= 100;
            segment.rotation = total_rotation;
            segment.time = handengine_time;
            segment.frame = frame_count;
            auto lookup = HAND_ENGINE_TO_XSENS_SEGMENT_IDS.find(name);
            THALAMUS_ASSERT(lookup != HAND_ENGINE_TO_XSENS_SEGMENT_IDS.end());
            segment.segment_id = lookup->second;
          }
          std::sort(_segments.begin(), _segments.end(), [](auto& a, auto& b) {
            return a.segment_id < b.segment_id;
          });

          _segments[0].rotation = boost::qvm::quat<float>{1, 0, 0, 0};
          _segments[0].position = boost::qvm::vec<float, 3>{0,0,0};
          _segments[1].rotation = _segments[0].rotation*_segments[1].rotation;
          _segments[1].position = _segments[0].position + _segments[0].rotation*_segments[1].position;
          _segments[2].rotation = _segments[1].rotation*_segments[2].rotation;
          _segments[2].position = _segments[1].position + _segments[1].rotation*_segments[2].position;
          _segments[3].rotation = _segments[2].rotation*_segments[3].rotation;
          _segments[3].position = _segments[2].position + _segments[2].rotation*_segments[3].position;
          for(auto i = 0;i < 4;++i) {
            _segments[4 + i*4].rotation = _segments[0].rotation*_segments[4 + i*4].rotation;
            _segments[4 + i*4].position = _segments[0].position + _segments[0].rotation*_segments[4 + i*4].position;
            for(auto j = 1;j < 4;j++) {
              _segments[4 + i*4 + j].rotation = _segments[4 + i*4 + j - 1].rotation*_segments[4 + i*4 + j].rotation;
              _segments[4 + i*4 + j].position = _segments[4 + i*4 + j - 1].position + _segments[4 + i*4 + j - 1].rotation*_segments[4 + i*4 + j].position;
            }
          }

          thumb_distance = boost::qvm::mag(_segments[3].position - _segments[2].position);
          index_distance = boost::qvm::mag(_segments[7].position - _segments[5].position);
          middle_distance = boost::qvm::mag(_segments[11].position - _segments[9].position);
          ring_distance = boost::qvm::mag(_segments[15].position - _segments[13].position);
          pinky_distance = boost::qvm::mag(_segments[19].position - _segments[17].position);
          //_segments.resize(4);
          _segment_span = std::span<Segment const>(_segments.begin(), _segments.end());
          has_motion_data = true;
          has_analog_data = true;
          outer->ready(outer);

          ++frame_count;
          buffer_offset = 0;
          message_size = 0;
          buffer.resize(4);
        }
      }

      socket.async_receive(boost::asio::buffer(buffer.data() + buffer_offset, buffer.size() - buffer_offset), std::bind(&Impl::on_receive, this, _1, _2));
    }
  };

  HandEngineNode::HandEngineNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, graph, this)) {}

  HandEngineNode::~HandEngineNode() {}

  std::string HandEngineNode::type_name() {
    return "HAND_ENGINE";
  }

  std::span<MotionCaptureNode::Segment const> HandEngineNode::segments() const {
    return impl->_segment_span;
  }

  const std::string& HandEngineNode::pose_name() const {
    return impl->pose_name;
  }

  void HandEngineNode::inject(const std::span<Segment const>& segments) {
    impl->_segment_span = segments;
    ready(this);
  }

  std::chrono::nanoseconds HandEngineNode::time() const {
    return impl->time;
  }

  std::span<const double> HandEngineNode::data(int channel) const {
    switch(channel) {
      case 0:
        return std::span<const double>(&impl->pose_change, &impl->pose_change + 1);
      case 1:
        return std::span<const double>(&impl->thumb_distance, &impl->thumb_distance + 1);
      case 2:
        return std::span<const double>(&impl->index_distance, &impl->index_distance + 1);
      case 3:
        return std::span<const double>(&impl->middle_distance, &impl->middle_distance + 1);
      case 4:
        return std::span<const double>(&impl->ring_distance, &impl->ring_distance + 1);
      case 5:
        return std::span<const double>(&impl->pinky_distance, &impl->pinky_distance + 1);
      default:
        THALAMUS_ASSERT(false, "Unexpected channel: %d", channel);
    }
  }

  static std::string THUMB_DISTANCE("Thumb Distance (m)");
  static std::string INDEX_DISTANCE("Index Distance (m)");
  static std::string MIDDLE_DISTANCE("Middle Distance (m)");
  static std::string RING_DISTANCE("Ring Distance (m)");
  static std::string PINKY_DISTANCE("Pinky Distance (m)");
  static thalamus::vector<std::string> CHANNEL_NAMES{
    POSE_CHANGE, THUMB_DISTANCE, INDEX_DISTANCE, MIDDLE_DISTANCE, RING_DISTANCE, PINKY_DISTANCE};

  int HandEngineNode::num_channels() const {
    return CHANNEL_NAMES.size();
  }

  std::string_view HandEngineNode::name(int channel) const {
    return CHANNEL_NAMES.at(channel);
  }
  std::span<const std::string> HandEngineNode::get_recommended_channels() const {
    return std::span<const std::string>(CHANNEL_NAMES.begin(), CHANNEL_NAMES.end());
  }

  std::chrono::nanoseconds HandEngineNode::sample_interval(int) const {
    return impl->frame_interval;
  }

  void HandEngineNode::inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
    THALAMUS_ASSERT(spans.size() == 1);
    THALAMUS_ASSERT(spans.front().size() == 1);
  }
   
  bool HandEngineNode::has_analog_data() const {
    return impl->has_analog_data;
  }

  bool HandEngineNode::has_motion_data() const {
    return impl->has_motion_data;
  }

  size_t XsensNode::modalities() const { return infer_modalities<XsensNode>(); }
  size_t HandEngineNode::modalities() const { return infer_modalities<HandEngineNode>(); }
}
