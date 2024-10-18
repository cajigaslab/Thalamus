#include <nidaq_node.hpp>
#include <regex>
#include <absl/strings/numbers.h>
#include <tracing/tracing.hpp>
#include <numeric>
#include <grpc_impl.hpp>
#include <modalities_util.hpp>

namespace thalamus {
  struct NidaqNode::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::asio::io_context& io_context;
    boost::asio::high_resolution_timer timer;
    size_t analog_buffer_position;
    std::vector<double> analog_buffer;
    std::vector<double> output_buffer;
    std::vector<std::span<double const>> spans;
    size_t _num_channels;
    size_t buffer_size;
    std::chrono::nanoseconds _sample_interval;
    std::chrono::milliseconds _polling_interval;
    double _sample_rate;
    size_t counter = 0;
    std::chrono::nanoseconds _time = 0ns;
    std::atomic_bool busy;
    int _every_n_samples;
    std::list<std::vector<double>> buffers;
    NodeGraph* graph;
    NidaqNode* outer;
    bool is_running;
    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, NidaqNode* outer)
      : state(state)
      , io_context(io_context)
      , timer(io_context)
      , busy(false)
      , analog_buffer_position(0)
      , graph(graph)
      , outer(outer)
      , is_running(false) {
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [] {});
    }

    void NidaqCallback(const boost::system::error_code& error) {
      if (error.value() == boost::asio::error::operation_aborted) {
        return;
      }
      BOOST_ASSERT(!error);
      if (!is_running) {
        return;
      }

      output_buffer.resize(this->_num_channels * this->_every_n_samples);

      for (auto i = 0; i < this->_every_n_samples; ++i) {
        for (auto c = 0u; c < this->_num_channels; ++c) {
          if (c == 0) {
            output_buffer[c * this->_every_n_samples + i] = std::sin(this->_time.count() / 1e9);
          }
          else {
            output_buffer[c * this->_every_n_samples + i] = std::sin(this->_time.count() / 1e9 + M_PI / 4);
          }
        }
        this->_time += this->_sample_interval;
      }
      spans.clear();
      for (auto channel = 0; channel < _num_channels; ++channel) {
        spans.emplace_back(output_buffer.begin() + channel * _every_n_samples, output_buffer.begin() + (channel + 1) * _every_n_samples);
      }

      outer->ready(outer);

      this->timer.expires_after(_polling_interval);
      this->timer.async_wait(std::bind(&Impl::NidaqCallback, this, _1));
      return;
    }

    void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Running") {
        is_running = std::get<bool>(v);
        if (is_running) {
          counter = 0;
          std::string channel = state->at("Channel");
          _sample_rate = state->at("Sample Rate");
          _sample_interval = std::chrono::nanoseconds(size_t(1 / _sample_rate * 1e9));

          size_t polling_interval_raw = state->at("Poll Interval");
          _polling_interval = std::chrono::milliseconds(polling_interval_raw);

          _every_n_samples = int(_sample_rate * _polling_interval.count() / 1000);
          _num_channels = get_num_channels(channel);
          buffer_size = static_cast<size_t>(_sample_rate * _num_channels);
          std::function<void()> reader;

          _time = 0ns;

          NidaqCallback(boost::system::error_code());
        }
      }
    }
  };

  NidaqNode::NidaqNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph) : impl(new Impl(state, io_context, graph, this)) {}

  NidaqNode::~NidaqNode() {}

  int NidaqNode::get_num_channels(const std::string& channel) {
    std::regex regex("[a-zA-Z0-9/]+(\\d+)(:(\\d+))?$");
    std::smatch match_result;
    if (!std::regex_search(channel, match_result, regex)) {
      //QMessageBox::warning(nullptr, "Parse Failed", "Failed to parse NIDAQ channel");
      return -1;
    }
    if (!match_result[2].matched) {
      return 1;
    }
    else {
      auto left_str = match_result[1].str();
      auto right_str = match_result[3].str();
      int left, right;
      absl::SimpleAtoi(left_str, &left);
      absl::SimpleAtoi(right_str, &right);
      return right - left + 1;
    }
  }

  std::string NidaqNode::type_name() {
    return "NIDAQ (MOCK)";
  }

  std::span<const double> NidaqNode::data(int channel) const {
    return impl->spans.at(channel);
  }

  int NidaqNode::num_channels() const {
    return impl->_num_channels;
  }

  std::chrono::nanoseconds NidaqNode::sample_interval(int i) const {
    return impl->_sample_interval;
  }

  std::chrono::nanoseconds NidaqNode::time() const {
    return impl->_time;
  }

  void NidaqNode::inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>& names) {
    auto temp = impl->_num_channels;
    auto previous_sample_interval = impl->_sample_interval;
    impl->_num_channels = spans.size();
    impl->spans = spans;
    impl->_sample_interval = sample_intervals.at(0);
    ready(this);
    impl->_sample_interval = previous_sample_interval;
    impl->_num_channels = temp;
  }

  bool is_digital(const std::string& channel) {
    return channel.find("port") != std::string::npos;
  }

  struct NidaqOutputNode::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    NodeGraph* graph;
    std::weak_ptr<Node> source;
    boost::asio::io_context& io_context;
    std::thread nidaq_thread;
    std::mutex mutex;
    std::condition_variable condition_variable;
    std::vector<std::vector<double>> buffers;
    boost::asio::high_resolution_timer timer;
    std::vector<std::span<const double>> _data;
    size_t _num_channels;
    size_t buffer_size;
    boost::signals2::scoped_connection source_connection;
    std::map<size_t, std::function<void(Node*)>> observers;
    double _sample_rate;
    thalamus::vector<std::chrono::nanoseconds> _sample_intervals;
    size_t counter = 0;
    std::chrono::nanoseconds _time;
    NidaqOutputNode* outer;
    bool running;
    std::vector<std::chrono::steady_clock::time_point> next_write;
    std::vector<std::chrono::steady_clock::time_point> times;
    bool digital = false;
    std::vector<bool> digital_levels;
    std::atomic_bool new_buffers = false;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, NidaqOutputNode* outer)
      : state(state)
      , io_context(io_context)
      , timer(io_context)
      , graph(graph)
      , outer(outer)
      , running(false) {
      using namespace std::placeholders;
      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    ~Impl() {
      (*state)["Running"].assign(false, [&] {});
    }

    void on_change(ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if (key_str == "Source") {
        source_connection.disconnect();

        if (!state->contains("Source")) {
          return;
        }
        std::string source_str = state->at("Source");
        graph->get_node(source_str, [&](auto node) {
          source = node;
          auto locked_source = source.lock();
          if (!locked_source || node_cast<AnalogNode*>(locked_source.get()) == nullptr) {
            source.reset();
            return;
          }
          source_connection = locked_source->ready.connect(std::bind(&Impl::on_data, this, _1));
          });
      }
    }

    void on_data(Node* raw_node) {
      TRACE_EVENT0("thalamus", "NidaqOutputNode::on_data");
      auto node = reinterpret_cast<AnalogNode*>(raw_node);
      _data.clear();
      for (auto i = 0; i < node->num_channels(); ++i) {
        auto next = node->data(i);
        _data.push_back(next);
      }
      _time = node->time();

      outer->ready(outer);
    }
  };

  NidaqOutputNode::NidaqOutputNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph) : impl(new Impl(state, io_context, graph, this)) {}
  NidaqOutputNode::~NidaqOutputNode() {}

  std::string NidaqOutputNode::type_name() {
    return "NIDAQ_OUT (MOCK)";
  }

  size_t NidaqNode::modalities() const { return infer_modalities<NidaqNode>(); }

  bool NidaqNode::prepare() { return true; }
  bool NidaqOutputNode::prepare() { return true; }
  std::string_view NidaqNode::name(int) const {
    return "0";
  }
}
