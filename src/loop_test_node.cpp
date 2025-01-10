#include <loop_test_node.hpp>
#include <modalities_util.hpp>
#include <tracing/tracing.hpp>
#include <boost/asio.hpp>


namespace thalamus {
  using namespace std::chrono_literals;

  struct LoopTestNode::Impl {
    boost::asio::io_context& io_context;
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    boost::signals2::scoped_connection data_connection;
    boost::signals2::scoped_connection channels_changed_connection;
    boost::signals2::scoped_connection get_node_connection;
    LoopTestNode* outer;
    std::chrono::nanoseconds time = 0ns;
    NodeGraph* graph;
    AnalogNode* source;
    std::string source_channel_name;
    int source_channel_index = -1;
    double frequency = 0;
    boost::asio::steady_timer timer;
    double value = 0;

    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, LoopTestNode* outer)
      : state(state)
      , outer(outer)
      , io_context(io_context)
      , graph(graph)
      , timer(io_context) {
      using namespace std::placeholders;

      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));

      boost::asio::co_spawn(io_context, generate(), boost::asio::detached);
    }

    boost::asio::awaitable<void> generate() {
      auto interval = 1ms;
      auto synth_time = 0.0;
      while(true) {
        TRACE_EVENT0("thalamus", "LoopTestNode_loop");
        timer.expires_after(interval);
        try {
          co_await timer.async_wait();
        } catch (boost::system::system_error& e) {
          if (boost::system::errc::operation_canceled == e.code())
          {
            co_return;
          }
        }
        //generate sin wave using differential equation
        auto derivative = 2 * M_PI * frequency * std::cos(2*M_PI*synth_time);
        value += (1.0/decltype(interval)::period::den)* derivative;
        time += interval;
        synth_time += frequency * 1e-3;
        TRACE_EVENT0("thalamus", "LoopTestNode::ready");
        outer->ready(outer);
      }
    }

    ~Impl() {
      timer.cancel();
    }

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      TRACE_EVENT0("thalamus", "LoopTestNode::on_change");
      auto key_str = std::get<std::string>(k);
      if (key_str == "Source") {
        auto value_str = std::get<std::string>(v);
        absl::StripAsciiWhitespace(&value_str);
        get_node_connection = graph->get_node_scoped(value_str, [&](std::weak_ptr<Node> weak) {
          auto locked = weak.lock();
          auto analog = node_cast<AnalogNode*>(locked.get());
          if(!analog) {
            return;
          }
          source = analog;
          channels_changed_connection = source->channels_changed.connect([&](auto arg) {
            source_channel_index = -1;
          });
          channels_changed_connection = locked->ready.connect([&](auto arg) {
            if(!source->has_analog_data()) {
              return;
            }
            if(source_channel_index == -1) {
              for(auto i = 0;i < source->num_channels();++i) {
                if(source->name(i) == source_channel_name) {
                  source_channel_index = i;
                  break;
                }
              }
              if(source_channel_index == -1) {
                return;
              }
            }
            auto span = source->data(source_channel_index);
            if(!span.empty()) {
              frequency = span.back();
            }
          });
        });
      } else if(key_str == "Channel") {
        auto value_str = std::get<std::string>(v);
        absl::StripAsciiWhitespace(&value_str);
        source_channel_name = value_str;
        source_channel_index = -1;
      }
    }
  };

  LoopTestNode::LoopTestNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, graph, this)) {}

  LoopTestNode::~LoopTestNode() {}

  std::string LoopTestNode::type_name() {
    return "LOOP_TEST";
  }

  std::chrono::nanoseconds LoopTestNode::time() const {
    return impl->time;
  }

  std::span<const double> LoopTestNode::data(int index) const {
    return std::span<const double>(&impl->value, &impl->value+1);
  }

  int LoopTestNode::num_channels() const {
    return 1;
  }

  std::chrono::nanoseconds LoopTestNode::sample_interval(int channel) const {
    return 1000000ns;
  }

  std::string_view LoopTestNode::name(int channel) const {
    return "Sin";
  }

  void LoopTestNode::inject(const thalamus::vector<std::span<double const>>& data, const thalamus::vector<std::chrono::nanoseconds>& interval, const thalamus::vector<std::string_view>& names) {
  }

  bool LoopTestNode::has_analog_data() const {
    return true;
  }

  size_t LoopTestNode::modalities() const { return infer_modalities<LoopTestNode>(); }
}
