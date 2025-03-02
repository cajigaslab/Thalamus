#include <cstdint>
#include <modalities_util.hpp>
#include <sync_node.hpp>
#include <vector>

namespace thalamus {
struct SyncNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  std::map<std::string, std::pair<boost::signals2::scoped_connection,
                                  boost::signals2::scoped_connection>>
      sources_connections;
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
  SyncNode *outer;
  std::string address_str;
  bool is_connected = false;
  double amplitude;
  double value;
  std::chrono::milliseconds duration;
  std::chrono::nanoseconds current_time;
  ObservableListPtr channels;
  NodeGraph *graph;
  std::vector<std::weak_ptr<AnalogNode>> sources;
  size_t _max_channels = std::numeric_limits<size_t>::max();
  struct Pair {
    enum class Algo { THRESHOLD, CROSS_CORRELATION };
    ObservableDict *state = nullptr;
    Node *node1 = nullptr;
    std::string channel1_name;
    std::string node1_name;
    int channel1_index = -1;
    Node *node2 = nullptr;
    std::string channel2_name;
    std::string node2_name;
    int channel2_index = -1;
    Algo algo = Algo::THRESHOLD;
    double threshold = .5;
    std::chrono::nanoseconds window = 500'000'000ns;
    std::chrono::nanoseconds cross1;
    std::chrono::nanoseconds cross2;
    std::vector<double> data1;
    std::vector<double> data2;
    std::chrono::nanoseconds start_time1;
    std::chrono::nanoseconds start_time2;
    std::chrono::nanoseconds sample_interval1;
    std::chrono::nanoseconds sample_interval2;
    double lag = 0;
    std::string out_channel_name;
  };
  std::vector<Pair> pairs;
  std::map<std::string, boost::signals2::scoped_connection> data_connections;
  std::map<std::string, boost::signals2::scoped_connection>
      channels_connections;
  std::map<std::string, boost::signals2::scoped_connection> node_connections;
  ObservableCollection *pairs_state;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context &, NodeGraph *_graph,
       SyncNode *_outer)
      : state(_state), outer(_outer), graph(_graph) {

    state_connection = state->recursive_changed.connect(
        std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  void compute(AnalogNode *analog, std::vector<double> &pdata,
               int &channel_index, std::string &channel_name, double threshold,
               std::chrono::nanoseconds &cross,
               std::chrono::nanoseconds &start_time,
               std::chrono::nanoseconds &sample_interval, Pair::Algo algo) {
    if (channel_index == -1) {
      for (auto i = 0; i < analog->num_channels(); ++i) {
        if (analog->name(i) == channel_name) {
          channel_index = i;
          break;
        }
      }
    }
    if (channel_index == -1) {
      return;
    }
    auto data = analog->data(channel_index);
    if (data.empty()) {
      return;
    }
    sample_interval = analog->sample_interval(channel_index);
    auto time = analog->time() - (data.size() - 1) * sample_interval;
    if (algo == Pair::Algo::THRESHOLD) {
      size_t i;
      double last;
      if (pdata.empty()) {
        i = 1;
        last = data[0];
      } else {
        i = 0;
        last = pdata.back();
      }
      while (i < data.size()) {
        auto d = data[i++];
        if (last < threshold && d >= threshold) {
          cross = time;
          last = *(data.end() - 1);
          break;
        }
        time += sample_interval;
        last = d;
      }
      pdata.assign(1, last);
    } else {
      if (pdata.empty()) {
        start_time = time;
      }
      pdata.insert(pdata.end(), data.begin(), data.end());
    }
  }

  void on_data(AnalogNode *analog, Node *node) {
    if (!analog->has_analog_data()) {
      return;
    }
    auto publish = true;
    for (auto &p : pairs) {
      if (p.node1 == node) {
        compute(analog, p.data1, p.channel1_index, p.channel1_name, p.threshold,
                p.cross1, p.start_time1, p.sample_interval1, p.algo);
      }
      if (p.node2 == node) {
        compute(analog, p.data2, p.channel2_index, p.channel2_name, p.threshold,
                p.cross2, p.start_time2, p.sample_interval2, p.algo);
      }
      if (p.algo == Pair::Algo::THRESHOLD) {
        auto diff = p.cross2 - p.cross1;
        if (p.cross1 > 0ns && p.cross2 > 0ns &&
            abs(diff.count()) < p.window.count()) {
          p.lag = double(diff.count()) / 1e9;
          p.cross1 = 0ns;
          p.cross2 = 0ns;
          publish = true;
        }
      } else {
        long long data1_size = int64_t(p.data1.size());
        long long data2_size = int64_t(p.data2.size());
        auto window1_size = p.sample_interval1 > 0ns
                                ? p.sample_interval1 * data1_size
                                : (analog->time() - p.start_time1);
        auto window2_size = p.sample_interval2 > 0ns
                                ? p.sample_interval2 * data2_size
                                : (analog->time() - p.start_time2);
        auto sample_interval1 = p.sample_interval1 > 0ns
                                    ? p.sample_interval1
                                    : (window1_size / data1_size);
        auto sample_interval2 = p.sample_interval2 > 0ns
                                    ? p.sample_interval2
                                    : (window2_size / data2_size);
        if (window1_size > p.window && window2_size > p.window) {
          auto data1 = &p.data1;
          auto data2 = &p.data2;
          auto i = 0ull;
          auto j = 0ull;
          auto time1 = 0ns;
          auto time2 = 0ns;
          std::vector<double> resampled;
          if (sample_interval1 < sample_interval2) {
            while (resampled.size() < data1->size()) {
              if (time1 > time2 + sample_interval2) {
                time2 += sample_interval2;
                ++j;
              }
              time1 += sample_interval1;
              if (j < data2->size()) {
                resampled.push_back((*data2)[j]);
              } else {
                resampled.push_back(resampled.back());
              }
            }
            data2 = &resampled;
          } else if (sample_interval1 > sample_interval2) {
            while (resampled.size() < data2->size()) {
              if (time2 > time1 + sample_interval1) {
                time1 += sample_interval1;
                ++i;
              }
              time2 += sample_interval2;
              if (i < data1->size()) {
                resampled.push_back((*data1)[i]);
              } else {
                resampled.push_back(resampled.back());
              }
            }
            data1 = &resampled;
          }
          auto max = 0.0;
          auto max_index = 0;
          for (auto lag = -(int(data2->size()) - 1); lag < int(data1->size());
               ++lag) {
            i = size_t(std::max(0, -lag));
            j = size_t(std::max(0, lag));
            auto count = std::min(data2->size() - i, data1->size() - j);
            auto sum = 0.0;
            for (auto k = 0ull; k < count; ++k) {
              sum += (*data2)[i + k] * (*data1)[j + k];
            }
            if (sum > max) {
              max = sum;
              max_index = lag;
            }
          }
          p.lag = double((max_index * sample_interval1).count()) / 1e9;
          publish = true;
          p.data1.clear();
          p.data2.clear();
        }
      }
    }
    if (publish) {
      current_time = analog->time();
      outer->ready(outer);
    }
  }

  void on_channels_changed(AnalogNode *) {
    for (auto &p : pairs) {
      p.channel1_index = -1;
      p.channel2_index = -1;
    }
    outer->channels_changed(outer);
  }

  Pair &get_pair(ObservableCollection *source) {
    for (auto &p : pairs) {
      if (p.state == source) {
        return p;
      }
    }
    THALAMUS_ASSERT(false, "Failed to find pair");
  }

  void on_change(ObservableCollection *source,
                 ObservableCollection::Action action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    if (source == state.get()) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Pairs") {
        auto list = std::get<ObservableListPtr>(v);
        pairs_state = list.get();
        list->recap(std::bind(&Impl::on_change, this, pairs_state, _1, _2, _3));
      }
    } else if (source == pairs_state) {
      auto key_int = std::get<long long>(k);
      if (action == ObservableCollection::Action::Delete) {
        pairs.erase(pairs.begin() + key_int);
        return;
      }
      if (pairs.size() <= size_t(key_int)) {
        pairs.resize(size_t(key_int) + 1);
      }
      auto dict = std::get<ObservableDictPtr>(v);
      pairs[size_t(key_int)].state = dict.get();
      dict->recap(std::bind(&Impl::on_change, this, dict.get(), _1, _2, _3));
    } else {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Node 1" || key_str == "Node 2") {
        auto value_str = std::get<std::string>(v);
        node_connections[value_str] = graph->get_node_scoped(
            value_str, [this, key_str, value_str, source](auto node) {
              auto locked = node.lock();
              auto &pair = get_pair(source);
              if (key_str == "Node 1") {
                pair.node1 = locked.get();
                pair.node1_name = value_str;
              } else if (key_str == "Node 2") {
                pair.node2 = locked.get();
                pair.node2_name = value_str;
              }
              pair.out_channel_name = absl::StrFormat(
                  "%s[%s]-%s[%s]", pair.node1_name, pair.channel1_name,
                  pair.node2_name, pair.channel2_name);

              auto analog_node = node_cast<AnalogNode *>(locked.get());
              data_connections[value_str] = locked->ready.connect(
                  std::bind(&Impl::on_data, this, analog_node, _1));
              channels_connections[value_str] =
                  analog_node->channels_changed.connect(
                      std::bind(&Impl::on_channels_changed, this, _1));
              outer->channels_changed(outer);
            });
      } else if (key_str == "Channel 1") {
        auto &pair = get_pair(source);
        auto value_str = std::get<std::string>(v);
        pair.channel1_name = value_str;
        pair.out_channel_name = absl::StrFormat(
            "%s[%s]-%s[%s]", pair.node1_name, pair.channel1_name,
            pair.node2_name, pair.channel2_name);
        outer->channels_changed(outer);
      } else if (key_str == "Channel 2") {
        auto &pair = get_pair(source);
        auto value_str = std::get<std::string>(v);
        pair.channel2_name = value_str;
        pair.out_channel_name = absl::StrFormat(
            "%s[%s]-%s[%s]", pair.node1_name, pair.channel1_name,
            pair.node2_name, pair.channel2_name);
        outer->channels_changed(outer);
      } else if (key_str == "Threshold") {
        auto &pair = get_pair(source);
        pair.threshold = std::get<double>(v);
      } else if (key_str == "Window (s)") {
        auto &pair = get_pair(source);
        auto milliseconds = int64_t(1e3 * std::get<double>(v));
        pair.window = std::chrono::milliseconds(milliseconds);
      }
    }
  }
};

SyncNode::SyncNode(ObservableDictPtr state, boost::asio::io_context &io_context,
                   NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

SyncNode::~SyncNode() {}

std::string SyncNode::type_name() { return "SYNC"; }

std::chrono::nanoseconds SyncNode::time() const { return impl->current_time; }

std::span<const double> SyncNode::data(int channel) const {
  auto &pair = impl->pairs[size_t(channel)];
  return std::span<const double>(&pair.lag, &pair.lag + 1);
}

int SyncNode::num_channels() const { return int(impl->pairs.size()); }

std::string_view SyncNode::name(int channel) const {
  return impl->pairs[size_t(channel)].out_channel_name;
}

std::chrono::nanoseconds SyncNode::sample_interval(int) const { return 0ns; }

void SyncNode::inject(const thalamus::vector<std::span<double const>> &,
                      const thalamus::vector<std::chrono::nanoseconds> &,
                      const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

bool SyncNode::has_analog_data() const { return true; }

size_t SyncNode::modalities() const { return infer_modalities<SyncNode>(); }
} // namespace thalamus
