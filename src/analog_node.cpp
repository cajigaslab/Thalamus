#include <thalamus/tracing.hpp>
#include <analog_node.hpp>
#include <cstdint>
#include <modalities_util.hpp>
#include <random>

using namespace thalamus;

AnalogNode::~AnalogNode() {}

struct WaveGeneratorNode::Impl {
  ObservableDictPtr state;
  ObservableList *nodes;
  boost::signals2::scoped_connection state_connection;
  NodeGraph *graph;
  std::weak_ptr<Node> source;
  boost::asio::io_context &io_context;
  boost::asio::high_resolution_timer timer;
  std::vector<std::vector<double>> buffers;
  thalamus::vector<std::span<const double>> spans;
  thalamus::vector<std::chrono::nanoseconds> sample_intervals;
  thalamus::vector<std::string> names;
  thalamus::vector<std::string_view> name_views;
  std::vector<std::string> recommended_names;
  size_t _num_channels;
  size_t buffer_size;
  size_t source_observer_id;
  std::map<size_t, std::function<void(Node *)>> observers;
  size_t counter = 0;
  double current = 0;
  std::random_device random_device;
  std::mt19937 random_range;
  std::uniform_int_distribution<std::mt19937::result_type> random_distribution;
  double frequency;
  double amplitude;
  double phase;
  size_t poll_interval;
  bool is_running;
  std::chrono::nanoseconds _sample_interval;
  std::chrono::nanoseconds _time;
  std::chrono::steady_clock::time_point last_time;
  std::chrono::steady_clock::time_point _start_time;
  AnalogNodeImpl analog_impl;
  WaveGeneratorNode *outer;
  std::string shape;
  double offset;
  double duty_cycle;
  std::function<double(std::chrono::nanoseconds)> wave;
  struct Wave {
    enum class Shape { SINE, SQUARE, TRIANGLE, RANDOM };
    Shape shape = Shape::SINE;
    double frequency = 1;
    double amplitude = 1;
    long long phase = 0;
    long long interval = 1e9;
    double duty_cycle = .5;
    long long duty_interval = .5e9;
    double offset = 0;
    double current = 0;
    std::chrono::nanoseconds last_switch;
    Impl *impl;

    Wave() = delete;
    Wave(Impl *_impl) : impl(_impl) {}

    double operator()(std::chrono::nanoseconds time) {
      switch (shape) {
      case Shape::SINE: {
        return amplitude *
                   std::sin(2 * M_PI *
                            (frequency * double(time.count()) + double(phase)) /
                            1e9) +
               offset;
      }
      case Shape::SQUARE: {
        auto modulo = (time.count() - phase) % interval;
        return (modulo < duty_interval ? amplitude : -amplitude) + offset;
      }
      case Shape::TRIANGLE: {
        auto quarter_interval = interval / 4;
        auto three_quarter_interval = 3 * quarter_interval;
        auto modulo = (time.count() - int(1e9 * double(phase))) % interval;
        if (modulo < quarter_interval) {
          return double(modulo) / double(quarter_interval) * amplitude + offset;
        } else if (modulo < three_quarter_interval) {
          return (1 - (double(modulo) - double(quarter_interval)) /
                          double(quarter_interval)) *
                     amplitude +
                 offset;
        } else {
          return (double(modulo) - double(three_quarter_interval)) /
                     double(quarter_interval) * amplitude -
                 amplitude + offset;
        }
      }
      case Shape::RANDOM: {
        if ((time - last_switch).count() > interval) {
          current = amplitude * double(impl->random_distribution(impl->random_range)) +
                    offset;
          last_switch = time;
        }
        return current;
      }
      }
    }
  };
  std::optional<Wave> default_wave;
  std::vector<Wave> waves;
  ObservableListPtr waves_state;
  size_t last_wave_count = 0;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, WaveGeneratorNode *_outer)
      : state(_state), nodes(static_cast<ObservableList *>(state->parent)),
        graph(_graph), io_context(_io_context), timer(io_context),
        recommended_names(1, "0"), random_range(random_device()),
        random_distribution(0, 1), outer(_outer) {
    analog_impl.inject({std::span<double const>()}, {0ns}, {""});
    state_connection = state->recursive_changed.connect(
        std::bind(&Impl::on_change, this, _1, _2, _3, _4));

    analog_impl.ready.connect([_outer](Node *) { _outer->ready(_outer); });

    this->state->recap(
        std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  void on_timer(const boost::system::error_code &error) {
    TRACE_EVENT("thalamus", "WaveGeneratorNode::on_timer");
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);

    if (!_sample_interval.count()) {
      timer.expires_after(std::chrono::milliseconds(poll_interval));
      timer.async_wait(std::bind(&Impl::on_timer, this, _1));
      return;
    }

    auto wave_count = waves.size();
    if (wave_count != last_wave_count) {
      outer->channels_changed(outer);
    }
    last_wave_count = wave_count;

    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - _start_time);
    last_time = now;
    buffers.resize(wave_count);
    spans.clear();

    auto i = 0ull;
    auto new_time = _time;
    for (auto &w : waves) {
      auto &buffer = buffers[i];
      buffer.clear();
      new_time = _time;
      while (new_time <= elapsed) {
        buffer.push_back(w(new_time));
        new_time += _sample_interval;
      }
      ++i;
      spans.emplace_back(buffer.begin(), buffer.end());
    }
    sample_intervals.assign(spans.size(), _sample_interval);
    for (auto j = names.size(); j < i; ++j) {
      names.emplace_back(std::to_string(j));
    }
    name_views.assign(names.begin(), names.end());

    analog_impl.inject(spans, sample_intervals, name_views,
                       now.time_since_epoch());
    _time = new_time;
    // auto after = std::chrono::steady_clock::now();
    // std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(after
    // - now).count() << std::endl;
    if (!is_running) {
      return;
    }
    timer.expires_after(std::chrono::milliseconds(poll_interval));
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  void on_change(ObservableCollection *source_collection,
                 ObservableCollection::Action action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    TRACE_EVENT("thalamus", "WaveGeneratorNode::on_change");
    std::string key_str;
    Wave *selected_wave = nullptr;
    if (source_collection == state.get()) {
      key_str = std::get<std::string>(k);
      if (key_str == "Waves") {
        waves_state = std::get<ObservableListPtr>(v);
        waves_state->recap(
            std::bind(&Impl::on_change, this, waves_state.get(), _1, _2, _3));
        return;
      }
      if (waves.empty()) {
        waves.emplace_back(this);
      }
      selected_wave = &waves[0];
    } else if (source_collection == waves_state.get()) {
      if (action == ObservableCollection::Action::Set) {
        auto wave_state = std::get<ObservableDictPtr>(v);
        wave_state->recap(
            std::bind(&Impl::on_change, this, wave_state.get(), _1, _2, _3));
      } else {
        auto i = std::get<long long>(k);
        waves.erase(waves.begin() + i);
      }
      return;
    } else if (source_collection->parent == waves_state.get()) {
      key_str = std::get<std::string>(k);
      auto temp = waves_state->key_of(*source_collection);
      THALAMUS_ASSERT(temp.has_value(), "Failed to find index of wave");
      auto index = size_t(std::get<long long>(*temp));
      if (waves.size() <= index) {
        waves.resize(index + 1, Wave(this));
      }
      selected_wave = &waves[index];
    } else {
      return;
    }

    if (key_str == "Frequency") {
      selected_wave->frequency = std::get<double>(v);
      selected_wave->interval = int64_t(1e9 / selected_wave->frequency);
      selected_wave->duty_interval =
          int64_t(double(selected_wave->interval) * selected_wave->duty_cycle);
    } else if (key_str == "Amplitude") {
      selected_wave->amplitude = std::get<double>(v);
    } else if (key_str == "Shape") {
      shape = std::get<std::string>(v);
      if (shape == "Sine") {
        selected_wave->shape = Wave::Shape::SINE;
      } else if (shape == "Square") {
        selected_wave->shape = Wave::Shape::SQUARE;
      } else if (shape == "Triangle") {
        selected_wave->shape = Wave::Shape::TRIANGLE;
      } else if (shape == "Random") {
        selected_wave->shape = Wave::Shape::RANDOM;
      } else {
        selected_wave->shape = Wave::Shape::SINE;
      }
    } else if (key_str == "Offset") {
      selected_wave->offset = std::get<double>(v);
    } else if (key_str == "Duty Cycle") {
      selected_wave->duty_cycle = std::get<double>(v);
      selected_wave->duty_interval =
          int64_t(double(selected_wave->interval) * selected_wave->duty_cycle);
    } else if (key_str == "Phase") {
      selected_wave->phase = int64_t(1e9 * std::get<double>(v));
    } else if (key_str == "Poll Interval") {
      poll_interval = size_t(std::get<long long int>(v));
    } else if (key_str == "Sample Rate") {
      auto sample_rate = std::get<double>(v);
      _sample_interval = std::chrono::nanoseconds(
          static_cast<std::chrono::nanoseconds::rep>(1e9 / sample_rate));
      outer->channels_changed(outer);
    } else if (key_str == "Running") {
      is_running = std::get<bool>(v);
      for (auto &w : waves) {
        w.last_switch = 0ns;
      }
      if (is_running) {
        last_time = std::chrono::steady_clock::now();
        _start_time = last_time;
        _time = 0ns;
        on_timer(boost::system::error_code());
      }
    }
  }
};

WaveGeneratorNode::WaveGeneratorNode(ObservableDictPtr state,
                                     boost::asio::io_context &io_context,
                                     NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

WaveGeneratorNode::~WaveGeneratorNode() {
  (*impl->state)["Running"].assign(false, [] {});
}

std::string WaveGeneratorNode::type_name() { return "WAVE"; }

std::string_view WaveGeneratorNode::name(int channel) const {
  return impl->analog_impl.name(channel);
}
std::span<const std::string>
WaveGeneratorNode::get_recommended_channels() const {
  return std::span<const std::string>(impl->recommended_names.begin(),
                                      impl->recommended_names.end());
}

std::span<const double> WaveGeneratorNode::data(int index) const {
  return impl->analog_impl.data(index);
}

int WaveGeneratorNode::num_channels() const {
  return impl->analog_impl.num_channels();
}

void WaveGeneratorNode::inject(
    const thalamus::vector<std::span<double const>> &data,
    const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
    const thalamus::vector<std::string_view> &names) {
  impl->analog_impl.inject(data, sample_intervals, names);
}

std::chrono::nanoseconds WaveGeneratorNode::sample_interval(int channel) const {
  return impl->analog_impl.sample_interval(channel);
}
std::chrono::nanoseconds WaveGeneratorNode::time() const {
  return impl->analog_impl.time();
}

struct AnalogNodeImpl::Impl {
  thalamus::vector<std::string_view> names;
  thalamus::vector<std::span<double const>> spans;
  thalamus::vector<std::chrono::nanoseconds> sample_intervals;
  std::chrono::nanoseconds time;
};

AnalogNodeImpl::AnalogNodeImpl(ObservableDictPtr, boost::asio::io_context &,
                               NodeGraph *)
    : impl(new Impl()) {}
AnalogNodeImpl::AnalogNodeImpl() : impl(new Impl()) {}
AnalogNodeImpl::~AnalogNodeImpl() {}
std::span<const double> AnalogNodeImpl::data(int channel) const {
  return impl->spans.at(size_t(channel));
}
int AnalogNodeImpl::num_channels() const { return int(impl->spans.size()); }
std::chrono::nanoseconds AnalogNodeImpl::sample_interval(int channel) const {
  return impl->sample_intervals.at(size_t(channel));
}
std::chrono::nanoseconds AnalogNodeImpl::time() const { return impl->time; }

std::string_view AnalogNodeImpl::name(int channel) const {
  return impl->names.at(size_t(channel));
}
std::span<const std::string> AnalogNodeImpl::get_recommended_channels() const {
  return std::span<const std::string>();
}

void AnalogNodeImpl::inject(
    const thalamus::vector<std::span<double const>> &spans,
    const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
    const thalamus::vector<std::string_view> &names) {
  inject(spans, sample_intervals, names,
         std::chrono::steady_clock::now().time_since_epoch());
}
void AnalogNodeImpl::inject(
    const thalamus::vector<std::span<double const>> &spans,
    const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
    const thalamus::vector<std::string_view> &names,
    std::chrono::nanoseconds now) {
  impl->spans.assign(spans.begin(), spans.end());
  impl->sample_intervals.assign(sample_intervals.begin(),
                                sample_intervals.end());
  impl->names = names;
  impl->time = now;
  ready(this);
}
std::string AnalogNodeImpl::type_name() { return "ANALOG"; }

struct ToggleNode::Impl {
  ObservableList *nodes;
  ObservableDictPtr state;
  std::weak_ptr<Node> source;
  NodeGraph *graph;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection source_connection;
  std::map<size_t, std::function<void(Node *)>> observers;
  boost::asio::io_context &io_context;
  thalamus::vector<double> buffer;
  std::list<thalamus::vector<double>> previous_buffers;
  double threshold = 1.6;
  bool high = false;
  double last_sample = 0;
  std::chrono::nanoseconds _time;
  size_t channel = 0;
  ToggleNode *outer;
  std::chrono::nanoseconds last_toggle;
  std::chrono::nanoseconds current_time;
  AnalogNodeImpl analog_impl;
  thalamus::vector<std::string> recommended_names;
  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       NodeGraph *_graph, ToggleNode *_outer)
      : nodes(static_cast<ObservableList *>(_state->parent)), state(_state),
        graph(_graph), io_context(_io_context), outer(_outer),
        recommended_names(1, "0") {
    using namespace std::placeholders;
    analog_impl.inject({{std::span<double const>()}}, {0ns}, {""});

    analog_impl.ready.connect([_outer](Node *) { _outer->ready(_outer); });

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  std::optional<double> previous_value(int current, int lag) {
    auto index = current - lag;
    if (index >= 0) {
      return buffer.at(size_t(index));
    }
    for (auto i = previous_buffers.rbegin(); i != previous_buffers.rend();
         ++i) {
      long long prev_index = int64_t(i->size()) + index;
      if (prev_index >= 0) {
        return i->at(size_t(prev_index));
      }
      index += i->size();
    }
    return std::nullopt;
  }

  void on_data(Node *, AnalogNode *node) {
    TRACE_EVENT("thalamus", "ToggleNode::Impl::on_data");
    auto source_data = node->data(int(channel));

    buffer.assign(source_data.begin(), source_data.end());
    auto sample_interval = node->sample_interval(int(channel));
    _time = node->time();

    auto lag_time = 100ms;
    auto lag = lag_time / sample_interval;
    auto total = 0;
    for (auto i = previous_buffers.rbegin(); i != previous_buffers.rend();
         ++i) {
      if (total > lag + 1) {
        previous_buffers.erase(previous_buffers.begin(), i.base());
        break;
      }
      total += i->size();
    }

    auto i = 0;
    std::transform(buffer.begin(), buffer.end(), buffer.begin(), [&](double d) {
      current_time += sample_interval;
      auto lagged = previous_value(i, int(lag));
      if (lagged && current_time - last_toggle > 2 * lag_time &&
          lagged < threshold && d >= threshold) {
        high = !high;
        last_toggle = current_time;
      }
      last_sample = d;
      ++i;
      return high ? 3.3 : 0;
    });
    previous_buffers.emplace_back(source_data.begin(), source_data.end());

    analog_impl.inject(
        {{std::span<double const>(buffer.begin(), buffer.end())}},
        {sample_interval}, {""});
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    using namespace std::placeholders;

    auto key_str = std::get<std::string>(k);
    if (key_str == "Source") {
      source_connection.disconnect();
      auto value_str = std::get<std::string>(v);
      source = graph->get_node(value_str);
      auto locked_source = source.lock();
      auto analog_node = dynamic_cast<AnalogNode *>(locked_source.get());
      if (!locked_source || analog_node == nullptr) {
        source.reset();
        return;
      }

      source_connection = locked_source->ready.connect(
          std::bind(&Impl::on_data, this, _1, analog_node));
    } else if (key_str == "Threshold") {
      if (std::holds_alternative<std::string>(v)) {
        threshold = atof(std::get<std::string>(v).c_str());
      } else {
        threshold = std::get<double>(v);
      }
    } else if (key_str == "Channel") {
      channel = state->at("Channel");
    }
  }
};

ToggleNode::ToggleNode(ObservableDictPtr state,
                       boost::asio::io_context &io_context, NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

ToggleNode::~ToggleNode() {}

std::string ToggleNode::type_name() { return "TOGGLE"; }

std::span<const double> ToggleNode::data(int i) const {
  return impl->analog_impl.data(i);
}
int ToggleNode::num_channels() const {
  return impl->analog_impl.num_channels();
}
std::string_view ToggleNode::name(int channel) const {
  return impl->recommended_names.at(size_t(channel));
}
std::span<const std::string> ToggleNode::get_recommended_channels() const {
  return std::span<const std::string>(impl->recommended_names.begin(),
                                      impl->recommended_names.end());
}

void ToggleNode::inject(
    const thalamus::vector<std::span<double const>> &data,
    const thalamus::vector<std::chrono::nanoseconds> &sample_intervals,
    const thalamus::vector<std::string_view> &names) {
  impl->analog_impl.inject(data, sample_intervals, names);
}

std::chrono::nanoseconds ToggleNode::sample_interval(int i) const {
  return impl->analog_impl.sample_interval(i);
}

std::chrono::nanoseconds ToggleNode::time() const { return impl->_time; }

size_t ToggleNode::modalities() const { return infer_modalities<ToggleNode>(); }
size_t WaveGeneratorNode::modalities() const {
  return infer_modalities<WaveGeneratorNode>();
}
size_t AnalogNodeImpl::modalities() const {
  return infer_modalities<AnalogNodeImpl>();
}
