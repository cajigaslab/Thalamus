#include <aruco_node.h>

using namespace thalamus;

struct ArucoNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  std::vector<Segment> _segments;
  std::span<Segment const> _segment_span;
  ArucoNode* outer;

  Impl(ObservableDictPtr state, boost::asio::io_context& io_context, ArucoNode* outer)
    : state(state)
    , outer(outer) {

    using namespace std::placeholders;
    state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  void on_change(ObservableCollection::Action, const ObservableCollection::Key& key, const ObservableCollection::Value& value) {
  }
};

ArucoNode::ArucoNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph*)
  : impl(new Impl(state, io_context, this)) {}

ArucoNode::~ArucoNode() {}

std::string ArucoNode::type_name() {
  return "XSENS";
}

std::span<ArucoNode::Segment const> ArucoNode::segments() const {
  return impl->_segment_span;
}
const std::string& ArucoNode::pose_name() const {
  return impl->pose_name;
}

void ArucoNode::inject(const std::span<Segment const>& segments) {
  impl->_segment_span = segments;
  ready(this);
}

const size_t MotionCaptureNode::Segment::serialized_size = 32;

ArucoNode::Segment ArucoNode::Segment::parse(unsigned char* data) {
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

std::chrono::nanoseconds ArucoNode::time() const {
  return impl->time;
}

std::span<const double> ArucoNode::data(int channel) const {
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

int ArucoNode::num_channels() const {
  return impl->channel_names.size();
}

std::string_view ArucoNode::name(int channel) const {
  return impl->channel_names.at(channel);
}
std::span<const std::string> ArucoNode::get_recommended_channels() const {
  return std::span<const std::string>(impl->channel_names.begin(), impl->channel_names.end());
}

std::chrono::nanoseconds ArucoNode::sample_interval(int) const {
  return impl->frame_interval;
}

void ArucoNode::inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
  THALAMUS_ASSERT(spans.size() == 1);
  THALAMUS_ASSERT(spans.front().size() == 1);
}
 
bool ArucoNode::has_analog_data() const {
  return impl->has_analog_data;
}

bool ArucoNode::has_motion_data() const {
  return true;
}

boost::json::value ArucoNode::process(const boost::json::value& value) {
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
