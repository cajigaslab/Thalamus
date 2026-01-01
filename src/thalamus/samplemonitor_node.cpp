#include <thalamus/tracing.hpp>

#include <thalamus/samplemonitor_node.hpp>
#include <thalamus/async.hpp>
#include <thalamus/modalities_util.hpp>
#include <vector>

#include <chrono>

namespace thalamus {
struct SampleMonitorNode::Impl {
  ObservableDictPtr state;
  ObservableListPtr nodes;
  boost::signals2::scoped_connection state_connection;
  SampleMonitorNode *outer;
  NodeGraph *graph;
  std::chrono::nanoseconds time = 0ns;
  MovableSteadyTimer timer;
  std::chrono::nanoseconds last_publish;
  bool changed = true;

  struct Connection {
    NodeGraph *graph;
    boost::signals2::scoped_connection get_node_connection;
    boost::signals2::scoped_connection data_connection;
    boost::signals2::scoped_connection changed_connection;
    bool changed = true;

    std::vector<std::string> names;
    std::vector<int> counts;
    std::vector<std::chrono::nanoseconds> sample_intervals;
    std::string node_name;
    std::vector<std::chrono::nanoseconds> start_time;
    std::vector<std::chrono::nanoseconds> end_time;
    
    Connection(NodeGraph *_graph) : graph(_graph) {}

    void set_name(const std::string& name) {
      node_name = name;
      data_connection.release();
      changed_connection.release();
      get_node_connection = graph->get_node_scoped(name, [&](auto weak) {
        auto node = weak.lock();
        auto analog_node = node_cast<AnalogNode*>(node.get());
        if(!analog_node) {
          return;
        }

        changed_connection = analog_node->channels_changed.connect([&](auto) {
          changed = true;
        });

        data_connection = node->ready.connect([&,analog_node](Node*) {
          if(!analog_node->has_analog_data()) {
            return;
          }

          auto num_channels = size_t(analog_node->num_channels());
          counts.resize(num_channels, 0);
          sample_intervals.resize(num_channels, 0ns);
          names.resize(num_channels, "");
          start_time.resize(num_channels, -1ns);
          end_time.resize(num_channels, -1ns);
          auto now = analog_node->time();

          visit_node(analog_node, [&](auto wrap_node) {
            for(size_t i = 0;i < num_channels;++i) {
              auto ii = int(i);
              if(start_time[i] < 0ns) {
                start_time[i] = now;
                continue;
              }
              end_time[i] = now;

              counts[i] += wrap_node->data(ii).size();
              sample_intervals[i] = analog_node->sample_interval(ii);

              if(names[i].empty()) {
                names[i] = node_name + " " + std::string(wrap_node->name(ii));
              }
            }
          });
        });
      });
    }
  };
  std::vector<Connection> connections;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context & _io_context,
       NodeGraph *_graph, SampleMonitorNode *_outer)
      : state(_state), outer(_outer), graph(_graph), timer(_io_context) {

    state_connection =
        state->recursive_changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));

    last_publish = MovableSteadyClock::now().time_since_epoch();
    timer.expires_after(1s);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  std::vector<double> measured;
  std::vector<double> expected;
  std::vector<double> difference;
  std::vector<std::string> names;
  std::chrono::nanoseconds last_alert = 0ns;
  std::chrono::milliseconds interval = 1s;

  void on_timer(const boost::system::error_code& error) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    THALAMUS_ASSERT(!error, "Unexpected error");

    time = MovableSteadyClock::now().time_since_epoch();

    size_t num_channels = 0;
    for(auto& connection : connections) {
      changed = changed || connection.changed;
      connection.changed = false;
      num_channels += connection.counts.size();
    }
    if(changed) {
      outer->channels_changed(outer);
      changed = false;
    }

    measured.assign(num_channels, 0.0);
    expected.assign(num_channels, 0.0);
    difference.assign(num_channels, 0.0);
    names.assign(3*num_channels, "");

    size_t i = 0;
    for(auto& connection : connections) {
      for(size_t j = 0;j < connection.counts.size();++j) {
        auto sample_interval_ns = connection.sample_intervals[j].count();
        auto start_time = connection.start_time[j];
        auto end_time = connection.end_time[j];
        if(sample_interval_ns == 0 || start_time < 0ns || end_time < 0ns) {
          continue;
        }
        auto duration = end_time - start_time;
        auto duration_s = double(duration.count())/1e9;

        auto expected_val = sample_interval_ns ? 1e9/double(connection.sample_intervals[j].count()) : 0.0;
        auto measured_val = connection.counts[j]/duration_s;
        if(time - last_alert > 10s && allowed_error > 0.0 && std::abs(measured_val - expected_val) > allowed_error*expected_val) {
          std::string_view message = "Sample rate is outside expected parameters";
          graph->log(message);
          if(alert) {
            thalamus_grpc::Dialog dialog;
            dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
            dialog.set_title("SAMPLE_MONITOR");
            dialog.set_message(message);
            graph->dialog(dialog);
          }
          last_alert = time;
        }
        expected[i] = expected_val;
        measured[i] = measured_val;
        difference[i] = measured[i] - expected[i];
        names[3*i] = connection.names[j] + " Measured";
        names[3*i+1] = connection.names[j] + " Expected";
        names[3*i+2] = connection.names[j] + " Difference";

        connection.counts[j] = 0;
        connection.start_time[j] = -1ns;
        connection.end_time[j] = -1ns;
        ++i;
      }
    }

    outer->ready(outer);

    last_publish = time;
    timer.expires_after(interval);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  bool alert = false;
  double allowed_error = -1.0;

  void on_change(ObservableCollection* source,
                 ObservableCollection::Action a,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    
    if(source == state.get()) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Nodes") {
        nodes = std::get<ObservableListPtr>(v);
        nodes->recap(std::bind(&Impl::on_change, this, nodes.get(), _1, _2, _3));
      } else if (key_str == "Alert") {
        alert = std::get<bool>(v);
      } else if (key_str == "Allowed Error (%)") {
        allowed_error = std::get<double>(v)/100.0;
      } else if (key_str == "Interval (s)") {
        interval = std::chrono::milliseconds(int(std::get<double>(v)*1000.0));
      }
    }

    if(source == nodes.get()) {
      auto key_int = std::get<long long>(k);
      if(a == ObservableCollection::Action::Set) {
        connections.insert(connections.begin() + key_int, Connection(graph));
        auto node = std::get<ObservableDictPtr>(v);
        node->recap(std::bind(&Impl::on_change, this, node.get(), _1, _2, _3));
      } else {
        connections.erase(connections.begin() + key_int);
      }
    }

    if(source->parent == nodes.get()) {
      auto key_str = std::get<std::string>(k);

      if(key_str == "Name") {
        auto val_str = std::get<std::string>(v);
        auto index_opt = nodes->key_of(*source);
        THALAMUS_ASSERT(index_opt, "Failed to lookup node");
        auto index = std::get<long long>(*index_opt);

        connections[size_t(index)].set_name(val_str);
      }
    }
  }
};

SampleMonitorNode::SampleMonitorNode(ObservableDictPtr state, boost::asio::io_context &io_context,
                 NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

SampleMonitorNode::~SampleMonitorNode() {}

std::string SampleMonitorNode::type_name() { return "SAMPLE_MONITOR"; }

std::chrono::nanoseconds SampleMonitorNode::time() const { return impl->time; }

std::span<const double> SampleMonitorNode::data(int channel) const {
  if (size_t(channel) < impl->measured.size()) {
    return std::span<const double>(impl->measured.begin() + channel,
                                   impl->measured.begin() + channel + 1);
  }
  channel -= impl->measured.size();
  if (size_t(channel) < impl->expected.size()) {
    return std::span<const double>(impl->expected.begin() + channel,
                                   impl->expected.begin() + channel + 1);
  }
  channel -= impl->expected.size();
  if (size_t(channel) < impl->difference.size()) {
    return std::span<const double>(impl->difference.begin() + channel,
                                   impl->difference.begin() + channel + 1);
  }
  return std::span<const double>(); 
}

int SampleMonitorNode::num_channels() const { return int(impl->measured.size() + impl->expected.size() + impl->difference.size()); }

std::string_view SampleMonitorNode::name(int channel) const {
  return impl->names[size_t(channel)];
}

std::span<const std::string> SampleMonitorNode::get_recommended_channels() const {
  THALAMUS_ASSERT(false, "Unimplemented");
}

std::chrono::nanoseconds SampleMonitorNode::sample_interval(int) const {
  return 0s;
}

void SampleMonitorNode::inject(const thalamus::vector<std::span<double const>> &,
                     const thalamus::vector<std::chrono::nanoseconds> &,
                     const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

bool SampleMonitorNode::has_analog_data() const { return true; }

boost::json::value SampleMonitorNode::process(const boost::json::value &) {
  return boost::json::value();
}

size_t SampleMonitorNode::modalities() const { return infer_modalities<SampleMonitorNode>(); }
} // namespace thalamus
