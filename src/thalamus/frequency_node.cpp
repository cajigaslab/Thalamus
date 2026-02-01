#include <thalamus/tracing.hpp>

#define THALAMUS_DECLARE_TRACER(name, category, label) \
  struct name { \
    name() { \
      TRACE_EVENT_BEGIN(category, label); \
    } \
    ~name() { \
      TRACE_EVENT_END(category); \
    } \
  }

THALAMUS_DECLARE_TRACER(TraceOnData, "thalamus", "FrequencyNode::on_data");
THALAMUS_DECLARE_TRACER(TraceReady, "thalamus", "FrequencyNode_ready");

#include <thalamus/frequency_node.hpp>
#include <thalamus/modalities_util.hpp>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <chrono>

namespace thalamus {
struct FrequencyNode::Impl {
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection channels_connection;
  FrequencyNode *outer;
  NodeGraph *graph;
  bool channels_changed = true;
  long long channel_number = -1;
  double expected_frequency = -1;
  double frequency_margin = -1;
  double frequency_std_margin = -1;
  double allowed_error_percent = -1;
  double expected_frequency_std = -1;
  double threshold = 2.5;
  bool high = false;
  std::chrono::nanoseconds time = 0ns;
  std::vector<std::chrono::nanoseconds> edges;
  int fail_count = 0;
  double frequency_mean;
  double frequency_std;
  bool alert = false;
  std::chrono::nanoseconds last_alert = 0ns;
  boost::signals2::scoped_connection get_source_connection;
  boost::signals2::scoped_connection source_connection;
  AnalogNode *source = nullptr;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context &,
       NodeGraph *_graph, FrequencyNode *_outer)
      : state(_state), outer(_outer), graph(_graph) {

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }


  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if(key_str == "Channel Number") {
      channel_number = std::get<long long>(v);
    } else if (key_str == "Expected Frequency") {
      expected_frequency = std::get<double>(v);
      frequency_margin = expected_frequency*(allowed_error_percent/100);
    } else if (key_str == "Expected Frequency Std") {
      expected_frequency_std = std::get<double>(v);
    } else if (key_str == "Allowed Error (%)") {
      allowed_error_percent = std::get<double>(v);
      frequency_margin = expected_frequency*(allowed_error_percent/100);
    } else if (key_str == "Threshold") {
      threshold = std::get<double>(v);
    } else if (key_str == "Alert") {
      alert = std::get<bool>(v);
    } else if (key_str == "Source") {
      auto value_str = std::get<std::string>(v);
      absl::StripAsciiWhitespace(&value_str);
      get_source_connection = graph->get_node_scoped(value_str, [&](auto node) {
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
          auto num_channels = source->num_channels();
          if(channel_number < 0 || num_channels <= channel_number) {
            return;
          }

          auto data = source->data(int(channel_number));
          auto sample_interval = source->sample_interval(int(channel_number));
          time = source->time();
          time -= (data.size()-1)*sample_interval;

          for(auto d : data) {
            if(high) {
              if(d < threshold) {
                high = false;
              }
            } else {
              if(d > threshold) {
                high = true;
                edges.push_back(time);
              }
            }
            time += sample_interval;
          }

          if(edges.size() > 1 && time - edges.front() >= 1s) {
            std::vector<double> frequencies;
            frequency_mean = 0.0;
            for(auto i = 1ull;i < edges.size();++i) {
              auto last_edge = edges[i-1];
              auto edge = edges[i];
              auto frequency = 1e9/double((edge - last_edge).count());
              frequencies.push_back(frequency);
              frequency_mean += frequency;
            }
            frequency_mean /= double(frequencies.size());
            frequency_std = 0.0;
            for(auto f : frequencies) {
              frequency_std += (f - frequency_mean)*(f - frequency_mean);
            }
            frequency_std /= double(frequencies.size());
            frequency_std = std::sqrt(frequency_std);

            if((expected_frequency > 0.0 && std::abs(frequency_mean - expected_frequency) > frequency_margin)
               || (expected_frequency_std > 0.0 && frequency_std > expected_frequency_std)) {
              ++fail_count;
            } else {
              fail_count = 0;
            }
            if(fail_count > 1 && time - last_alert > 10s) {
              last_alert = time;
              auto message = "Frequency is outside expected parameters";
              if(alert) {
                thalamus_grpc::Dialog dialog;
                dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
                dialog.set_title("Frequency Node");
                dialog.set_message(message);
                graph->dialog(dialog);
              }
              graph->log(message);
            }

            outer->ready(outer);
            edges.clear();
          }
        });
      });
    }
  }
};

FrequencyNode::FrequencyNode(ObservableDictPtr state, boost::asio::io_context &io_context,
                 NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

FrequencyNode::~FrequencyNode() {}

std::string FrequencyNode::type_name() { return "FREQUENCY"; }

std::chrono::nanoseconds FrequencyNode::time() const { return impl->time; }

std::span<const double> FrequencyNode::data(int channel) const {
  if(channel == 0) {
    return std::span<const double>(&impl->frequency_mean, &impl->frequency_mean + 1);
  } else {
    return std::span<const double>(&impl->frequency_std, &impl->frequency_std + 1);
  }
}

int FrequencyNode::num_channels() const { return 2; }

std::string_view FrequencyNode::name(int channel) const {
  if(channel == 0) {
    return "Mean";
  } else {
    return "Std";
  }
}

std::span<const std::string> FrequencyNode::get_recommended_channels() const {
  THALAMUS_ASSERT(false, "Unimplemented");
}

std::chrono::nanoseconds FrequencyNode::sample_interval(int) const {
  return 0ns;
}

void FrequencyNode::inject(const thalamus::vector<std::span<double const>> &,
                     const thalamus::vector<std::chrono::nanoseconds> &,
                     const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

bool FrequencyNode::has_analog_data() const { return true; }

boost::json::value FrequencyNode::process(const boost::json::value &) {
  return boost::json::value();
}

size_t FrequencyNode::modalities() const { return infer_modalities<FrequencyNode>(); }
} // namespace thalamus
