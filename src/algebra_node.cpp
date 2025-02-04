#include <thalamus/tracing.hpp>
#include <algebra_node.hpp>
#include <vector>
#include <calculator.hpp>
#include <boost/spirit/include/qi.hpp>
#include <modalities_util.hpp>
 
namespace thalamus {
  struct AlgebraNode::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    std::map<std::string, boost::signals2::scoped_connection> sources_connections;
    boost::signals2::scoped_connection channels_connection;
    size_t buffer_size;
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
    AlgebraNode* outer;
    std::string address_str;
    bool is_connected = false;
    double amplitude;
    double value;
    std::chrono::milliseconds duration;
    ObservableListPtr channels;
    NodeGraph* graph;
    std::vector<std::weak_ptr<AnalogNode>> sources;
    calculator::parser<std::string::const_iterator> parser;        // Our grammar
  public:
    Impl(ObservableDictPtr _state, boost::asio::io_context&, NodeGraph* _graph, AlgebraNode* _outer)
      : state(_state)
      , outer(_outer)
      , graph(_graph) {

      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    boost::signals2::scoped_connection source_connection;
    AnalogNode* source = nullptr;
    std::optional<calculator::program> program;    // Our program (AST)
    calculator::eval eval;
    std::vector<std::vector<double>> data;


    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Source") {
        auto value_str = std::get<std::string>(v);
        absl::StripAsciiWhitespace(&value_str);
        graph->get_node(value_str, [&](auto node) {
          auto locked = node.lock();
          if(!locked) {
            return;
          }
          source = std::dynamic_pointer_cast<AnalogNode>(locked).get();
          if(!source) {
            return;
          }
          source_connection = locked->ready.connect([&] (auto) {
            if(!source->has_analog_data()) {
              return;
            }
            if(data.size() < static_cast<size_t>(source->num_channels())) {
              data.resize(size_t(source->num_channels()));
            }
            for(auto i = 0;i < source->num_channels();++i) {
              auto span = source->data(i);
              auto& transformed = data.at(size_t(i));
              transformed.assign(span.begin(), span.end());
              if(!program) {
                continue;
              }
              for(auto j = 0ull;j < transformed.size();++j) {
                eval.symbols["X"] = transformed.at(j);
                eval.symbols["x"] = transformed.at(j);
                auto result = eval(*program);
                if(std::holds_alternative<double>(result)) {
                  transformed.at(j) = std::get<double>(result);
                } else {
                  transformed.at(j) = double(std::get<long long>(result));
                }
              }
            }
            outer->ready(outer);
          });
        });
      } else if (key_str == "Equation") {
        auto value_str = std::get<std::string>(v);
        boost::spirit::ascii::space_type space;
        auto iter = value_str.cbegin();
        auto success = phrase_parse(iter, value_str.cend(), parser, space, program);
        (*state)["Parser Error"].assign(!success);
      }
    }
  };

  AlgebraNode::AlgebraNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, graph, this)) {}

  AlgebraNode::~AlgebraNode() {}

  std::string AlgebraNode::type_name() {
    return "ALGEBRA";
  }

  std::chrono::nanoseconds AlgebraNode::time() const {
    return impl->source->time();
  }

  std::span<const double> AlgebraNode::data(int channel) const {
    auto& data = impl->data.at(size_t(channel));
    return std::span<const double>(data.begin(), data.end());
  }

  int AlgebraNode::num_channels() const {
    return int(impl->data.size());
  }

  std::string_view AlgebraNode::name(int channel) const {
    return impl->source->name(channel);
  }

  std::span<const std::string> AlgebraNode::get_recommended_channels() const {
    return impl->source ? impl->source->get_recommended_channels() : std::span<const std::string>();
  }

  std::chrono::nanoseconds AlgebraNode::sample_interval(int channel) const {
    return impl->source->sample_interval(channel);
  }

  void AlgebraNode::inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
    THALAMUS_ASSERT(false, "Unimplemented");
  }
   
  bool AlgebraNode::has_analog_data() const {
    return true;
  }

  size_t AlgebraNode::modalities() const { return infer_modalities<AlgebraNode>(); }
}

