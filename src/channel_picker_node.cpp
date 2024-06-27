#include <channel_picker_node.h>
#include <vector>

namespace thalamus {
  struct ChannelPickerNode::Impl {
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
    ChannelPickerNode* outer;
    std::string address_str;
    bool is_connected = false;
    double amplitude;
    double value;
    std::chrono::milliseconds duration;
    ObservableListPtr channels;
    NodeGraph* graph;
    std::vector<std::weak_ptr<AnalogNode>> sources;
    unsigned long long _max_channels = std::numeric_limits<unsigned long long>::max();
  public:
    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, ChannelPickerNode* outer)
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

    void on_source_mapping_change(std::weak_ptr<AnalogNode> node, long long in_channel, ObservableDictPtr mapping_dict, ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto k_str = std::get<std::string>(k);
      if(k_str == "Out Channel") {
        auto v_int = std::get<long long>(v);
        if(mappings.size() < v_int+1) {
          mappings.resize(v_int+1);
        }
        auto& mapping = mappings.at(v_int);
        std::get<std::weak_ptr<AnalogNode>>(mapping) = node;
        std::get<int>(mapping) = int(in_channel);
        if (mapping_dict->contains("Out Name")) {
          std::string temp = mapping_dict->at("Out Name");
          std::get<std::string>(mapping) = temp;
        }
      } else if (k_str == "Out Name") {
        if (!mapping_dict->contains("Out Channel")) {
          return;
        }
        long long out_channel = mapping_dict->at("Out Channel");
        if (mappings.size() < out_channel + 1) {
          mappings.resize(out_channel + 1);
        }
        auto& mapping = mappings.at(out_channel);
        std::string temp = mapping_dict->at("Out Name");
        std::get<std::string>(mapping) = temp;
      }
      outer->channels_changed(outer);
    }

    void on_source_mappings_change(std::weak_ptr<AnalogNode> node, ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto k_int = std::get<long long>(k);
      if(a == ObservableCollection::Action::Set) {
        auto v_dict = std::get<ObservableDictPtr>(v);
        
        single_source_map_connections.push_back(v_dict->changed.connect(std::bind(&Impl::on_source_mapping_change, this, node, k_int, v_dict, _1, _2, _3)));
        v_dict->recap(std::bind(&Impl::on_source_mapping_change, this, node, k_int, v_dict, _1, _2, _3));
      }
    }

    void on_sources_change(ObservableDictPtr sources_dict, ObservableCollection::Action a, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto node_name = std::get<std::string>(k);
      if(a == ObservableCollection::Action::Set) {
        auto v_list = std::get<ObservableListPtr>(v);
        graph->get_node(node_name, [&,node_name,v_list](auto node) {
          auto locked_source = node.lock();
          auto analog_node = std::dynamic_pointer_cast<AnalogNode>(locked_source);
          if (!locked_source || analog_node == nullptr) {
            return;
          }
          std::weak_ptr<AnalogNode> weak_analog(analog_node);

          source_mapping_connections[node_name] = v_list->changed.connect(std::bind(&Impl::on_source_mappings_change, this, weak_analog, _1, _2, _3));
          v_list->recap(std::bind(&Impl::on_source_mappings_change, this, weak_analog, _1, _2, _3));

          sources_connections[node_name].first = analog_node->channels_changed.connect([&,weak_analog](auto n) {
              auto locked = weak_analog.lock().get();
              names_collected.erase(locked);
              outer->channels_changed(outer);
          });
          sources_connections[node_name].second = locked_source->ready.connect([&,node_name,v_list,weak_analog](auto n) {
            current_node = weak_analog.lock().get();
            if(!current_node->has_analog_data()) {
              return;
            }
            if(!names_collected.contains(current_node)) {
              if(current_node) {
                for(auto i = 0;i < current_node->num_channels();++i) {
                  auto channel_name_view = current_node->name(i);
                  std::string channel_name(channel_name_view.data(), channel_name_view.size());
                  auto sample_interval = current_node->sample_interval(i);
                  if(i >= v_list->size()) {
                    auto new_row = std::make_shared<ObservableDict>();
                    (*new_row)["Name"].assign(channel_name);
                    (*new_row)["Out Channel"].assign(static_cast<long long>(mappings.size()));
                    (*new_row)["Out Name"].assign(node_name + ": " + channel_name);
                    v_list->at(i).assign(new_row);
                    mappings.emplace_back(weak_analog, i, node_name + ": " + channel_name, sample_interval);
                  } else {
                    ObservableDictPtr current_row = v_list->at(i);
                    (*current_row)["Name"].assign(channel_name);
                    long long out_channel = current_row->at("Out Channel");
                    auto& mapping = mappings.at(out_channel);
                    std::get<std::chrono::nanoseconds>(mapping) = sample_interval;
                  }
                }
                names_collected.insert(current_node);
              }
            }
            outer->ready(outer);
          });
        });
      } else if(a == ObservableCollection::Action::Delete) {
        source_mapping_connections.erase(node_name);
        sources_connections.erase(node_name);
      }
    }

    std::map<std::string, boost::signals2::scoped_connection> source_mapping_connections;
    std::vector<boost::signals2::scoped_connection> single_source_map_connections;
    boost::signals2::scoped_connection sources_state_connection;

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Sources") {
        auto sources_dict = std::get<ObservableDictPtr>(v);
        sources_connections.clear();
        sources.clear();
        sources_state_connection = sources_dict->changed.connect(std::bind(&Impl::on_sources_change, this, sources_dict, _1, _2, _3));
        sources_dict->recap(std::bind(&Impl::on_sources_change, this, sources_dict, _1, _2, _3));
      } else if (key_str == "Max Channels") {
        _max_channels = std::get<long long>(v);
      }
    }
  };

  ChannelPickerNode::ChannelPickerNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, graph, this)) {}

  ChannelPickerNode::~ChannelPickerNode() {}

  std::string ChannelPickerNode::type_name() {
    return "CHANNEL_PICKER";
  }

  std::chrono::nanoseconds ChannelPickerNode::time() const {
    return impl->current_node->time();
  }

  std::span<const double> ChannelPickerNode::data(int channel) const {
    if(num_channels() <= channel) {
      return std::span<const double>();
    }
    auto& pair = impl->mappings.at(channel);
    auto locked = std::get<0>(pair).lock();
    auto in_channel = std::get<1>(pair);
    return locked.get() == impl->current_node && in_channel < impl->current_node->num_channels() ? locked->data(std::get<1>(pair)) : std::span<const double>();
  }

  int ChannelPickerNode::num_channels() const {
    return std::min(impl->mappings.size(), impl->_max_channels);
  }

  static const std::string EMPTY = "";

  std::string_view ChannelPickerNode::name(int channel) const {
    if(num_channels() <= channel) {
      return EMPTY;
    }
    auto& pair = impl->mappings.at(channel);
    return std::get<2>(pair);
  }

  std::chrono::nanoseconds ChannelPickerNode::sample_interval(int channel) const {
    if(num_channels() <= channel) {
      return 0ns;
    }
    auto& pair = impl->mappings.at(channel);
    return std::get<std::chrono::nanoseconds>(pair);
  }

  void ChannelPickerNode::inject(const thalamus::vector<std::span<double const>>& spans, const thalamus::vector<std::chrono::nanoseconds>& sample_intervals, const thalamus::vector<std::string_view>& names) {
    THALAMUS_ASSERT(false);
  }
   
  bool ChannelPickerNode::has_analog_data() const {
    return true;
  }
}

