#include <normalize_node.hpp>
#include <vector>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/spirit/include/qi.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <modalities_util.hpp>

using Range = std::pair<double, double>;

namespace thalamus {
struct NormalizeNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  std::map<std::string, boost::signals2::scoped_connection> sources_connections;
  boost::signals2::scoped_connection channels_connection;
  size_t buffer_size;
  // double sample_rate;
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
  NormalizeNode *outer;
  std::string address_str;
  bool is_connected = false;
  double amplitude;
  double value;
  std::chrono::milliseconds duration;
  ObservableListPtr channels;
  NodeGraph *graph;
  std::vector<std::weak_ptr<AnalogNode>> sources;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context &, NodeGraph *_graph,
       NormalizeNode *_outer)
      : state(_state), outer(_outer), graph(_graph) {

    if (std::filesystem::exists(std::filesystem::path(".normalize_cache"))) {
      std::ifstream input(".normalize_cache", std::ios::in | std::ios::binary);
      input.read(reinterpret_cast<char *>(ranges.data()),
                 int64_t(sizeof(Range) * ranges.size()));
      print_ranges();
    }

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  boost::signals2::scoped_connection source_connection;
  AnalogNode *source = nullptr;
  std::vector<std::vector<double>> data;
  std::vector<Range> ranges;
  double out_min = 0;
  double out_max = 1;

  void print_ranges() {
    auto i = 0;
    for (auto &range : ranges) {
      THALAMUS_LOG(info) << ++i << " " << range.first << " " << range.second;
    }
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Max") {
      out_max = std::get<double>(v);
    } else if (key_str == "Min") {
      out_min = std::get<double>(v);
    } else if (key_str == "Source") {
      auto value_str = std::get<std::string>(v);
      absl::StripAsciiWhitespace(&value_str);
      graph->get_node(value_str, [&](auto node) {
        auto locked = node.lock();
        if (!locked) {
          return;
        }
        source = std::dynamic_pointer_cast<AnalogNode>(locked).get();
        if (!source) {
          return;
        }
        source_connection = locked->ready.connect([&](auto) {
          if (!source->has_analog_data()) {
            return;
          }
          if (int(data.size()) < source->num_channels()) {
            data.resize(size_t(source->num_channels()));
            ranges.resize(size_t(source->num_channels()),
                          std::make_pair(std::numeric_limits<double>::max(),
                                         -std::numeric_limits<double>::max()));
          }
          for (auto i = 0; i < source->num_channels(); ++i) {
            auto span = source->data(i);
            auto &transformed = data.at(size_t(i));
            auto &range = ranges.at(size_t(i));
            transformed.assign(span.begin(), span.end());
            for (size_t j = 0; j < transformed.size(); ++j) {
              auto &x = transformed.at(j);
              range.first = std::min(range.first, x);
              range.second = std::max(range.second, x);
              x = (x - range.first) /
                      (range.second - range.first +
                       std::numeric_limits<double>::min()) *
                      (out_max - out_min) +
                  out_min;
            }
          }
          outer->ready(outer);
        });
      });
    }
  }
};

NormalizeNode::NormalizeNode(ObservableDictPtr state,
                             boost::asio::io_context &io_context,
                             NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

NormalizeNode::~NormalizeNode() {}

std::string NormalizeNode::type_name() { return "NORMALIZE"; }

std::chrono::nanoseconds NormalizeNode::time() const {
  return impl->source->time();
}

std::span<const double> NormalizeNode::data(int channel) const {
  auto &data = impl->data.at(size_t(channel));
  return std::span<const double>(data.begin(), data.end());
}

int NormalizeNode::num_channels() const { return int(impl->data.size()); }

std::string_view NormalizeNode::name(int channel) const {
  return impl->source->name(channel);
}

std::span<const std::string> NormalizeNode::get_recommended_channels() const {
  return impl->source ? impl->source->get_recommended_channels()
                      : std::span<const std::string>();
}

std::chrono::nanoseconds NormalizeNode::sample_interval(int channel) const {
  return impl->source->sample_interval(channel);
}

void NormalizeNode::inject(const thalamus::vector<std::span<double const>> &,
                           const thalamus::vector<std::chrono::nanoseconds> &,
                           const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

bool NormalizeNode::has_analog_data() const { return true; }

boost::json::value NormalizeNode::process(const boost::json::value &value) {
  auto text = value.as_string();
  if (text == "Cache") {
    std::ofstream output(".normalize_cache",
                         std::ios::out | std::ios::binary | std::ios::ate);
    output.write(reinterpret_cast<char *>(impl->ranges.data()),
                 int64_t(sizeof(Range) * impl->ranges.size()));
    impl->print_ranges();
  } else if (text == "Reset") {
    impl->ranges.assign(impl->ranges.size(),
                        std::make_pair(std::numeric_limits<double>::max(),
                                       -std::numeric_limits<double>::max()));
  }
  return boost::json::value();
}
size_t NormalizeNode::modalities() const {
  return infer_modalities<NormalizeNode>();
}
} // namespace thalamus
