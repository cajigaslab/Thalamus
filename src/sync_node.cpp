#include <sync_node.hpp>
#include <vector>
#include <modalities_util.h>

namespace thalamus {
  struct SyncNode::Impl {
    ObservableDictPtr state;
    boost::signals2::scoped_connection state_connection;
    std::map<std::string, std::pair<boost::signals2::scoped_connection, boost::signals2::scoped_connection>> sources_connections;
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
    SyncNode* outer;
    std::string address_str;
    bool is_connected = false;
    double amplitude;
    double value;
    std::chrono::milliseconds duration;
    ObservableListPtr channels;
    NodeGraph* graph;
    std::vector<std::weak_ptr<AnalogNode>> sources;
    size_t _max_channels = std::numeric_limits<size_t>::max();
  public:
    Impl(ObservableDictPtr state, boost::asio::io_context&, NodeGraph* graph, SyncNode* outer)
      : state(state)
      , outer(outer)
      , graph(graph) {

      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    std::vector<std::tuple<std::weak_ptr<AnalogNode>, int, std::string, std::chrono::nanoseconds>> mappings;
    std::vector<std::string> recommended_channels;
    std::set<AnalogNode*> names_collected;
    AnalogNode* current_node;

    std::map<std::string, boost::signals2::scoped_connection> source_mapping_connections;
    std::vector<boost::signals2::scoped_connection> single_source_map_connections;
    boost::signals2::scoped_connection sources_state_connection;

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
    }
  };

  SyncNode::SyncNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, graph, this)) {}

  SyncNode::~SyncNode() {}

  std::string SyncNode::type_name() {
    return "SYNC";
  }

  std::chrono::nanoseconds SyncNode::time() const {
    return impl->current_node->time();
  }

  std::span<const double> SyncNode::data(int channel) const {
    return std::span<const double>();
  }

  int SyncNode::num_channels() const {
    return 0;
  }


  std::string_view SyncNode::name(int channel) const {
    return "";
  }

  std::chrono::nanoseconds SyncNode::sample_interval(int channel) const {
    if(num_channels() <= channel) {
      return 0ns;
    }
    auto& pair = impl->mappings.at(channel);
    return std::get<std::chrono::nanoseconds>(pair);
  }

  void SyncNode::inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
    THALAMUS_ASSERT(false);
  }
   
  bool SyncNode::has_analog_data() const {
    return true;
  }

  size_t SyncNode::modalities() const { return infer_modalities<SyncNode>(); }
}

