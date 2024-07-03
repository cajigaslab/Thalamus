#include <lua_node.h>
#include <vector>
#include <calculator.h>
#include <boost/spirit/include/qi.hpp>
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#include <chrono>

namespace thalamus {
  struct LuaNode::Impl {
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
    LuaNode* outer;
    std::string address_str;
    bool is_connected = false;
    double amplitude;
    double value;
    std::chrono::milliseconds duration;
    std::chrono::steady_clock::duration compute_time;
    ObservableListPtr channels;
    NodeGraph* graph;
    std::vector<std::weak_ptr<AnalogNode>> sources;
    calculator::parser<std::string::const_iterator> parser;        // Our grammar
    lua_State* L;
    boost::asio::steady_timer timer;
    bool did_compute = false;
    bool channels_changed = true;
    std::vector<std::string> channel_names;
    std::vector<std::chrono::nanoseconds> sample_intervals;
    std::chrono::nanoseconds time;
    std::string lua_namespace;
    ObservableListPtr equation_list;
    bool unsafe;
    static Impl* current;
    std::chrono::nanoseconds sample_time;
    std::chrono::nanoseconds sample_interval;
    size_t sample_index;
  public:
    Impl(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph, LuaNode* outer)
      : state(state)
      , outer(outer)
      , graph(graph)
      , timer(io_context) {

      timer.expires_after(1s);
      timer.async_wait(std::bind(&Impl::on_timer, this, _1));

      lua_namespace = "_" + std::to_string(rand());

      L = luaL_newstate();
      THALAMUS_ASSERT(L, "Failed to allocate Lua State");
      luaL_openlibs(L);

      lua_pushcfunction(L, lua_channel);
      lua_setglobal(L, "thalamus_channel");

      state_connection = state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
      state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
    }

    static int lua_channel(lua_State* L) {
      auto channel = luaL_checkinteger(L, 1);
      auto source = current->source;
      auto num_channels = source->num_channels();
      if(channel >= num_channels) {
        return luaL_error(L, "channel %d requested, only %d available", channel, num_channels);
      }
      auto span = source->data(channel);
      if(span.empty()) {
        lua_pushnumber(L, current->last_data[channel]);
        return 1;
      }
      auto channel_sample_interval = source->sample_interval(channel);
      size_t index = current->sample_index*current->sample_interval.count()/channel_sample_interval.count();
      index = std::min(index, span.size()-1);
      lua_pushnumber(L, span[index]);
      return 1;
    }

    void on_timer(const boost::system::error_code& error) {
      if(error) {
        return;
      }
      if(did_compute) {
        did_compute = false;
        time = std::chrono::steady_clock::now().time_since_epoch();
        for(auto& d : data) {
          d.resize(0);
        }
        data.back().push_back(std::chrono::duration_cast<std::chrono::milliseconds>(compute_time).count());
        compute_time = 0s;
        outer->ready(outer);
      }
      timer.expires_after(1s);
      timer.async_wait(std::bind(&Impl::on_timer, this, _1));
    }

    boost::signals2::scoped_connection source_connection;
    AnalogNode* source = nullptr;
    std::optional<calculator::program> program;    // Our program (AST)
    calculator::eval eval;
    std::vector<std::vector<double>> data;
    std::vector<double> last_data;

    void fill_stack(int n) {
      while(lua_gettop(L) < n) {
        lua_pushnil(L);
      }
    }

    void on_equation_change(ObservableDictPtr dict, long long index, ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto k_str = std::get<std::string>(k);
      if(k_str != "Equation") {
        return;
      }
      auto text = std::get<std::string>(v);
      absl::StripAsciiWhitespace(&text);
      if(text.empty()) {
        ((*dict)["Error"]).assign("");
        lua_pushnil(L);
        lua_copy(L, -1, index+1);
        lua_pop(L, 1);
        return;
      }
      auto func_name = lua_namespace + "_func" + std::to_string(index);
      std::stringstream stream;
      stream << "local channel = thalamus_channel\n";
      stream << "function " << func_name << "(x)\n";
      stream << "return " << text << "\n";
      stream << "end\n";

      fill_stack(index+1);

      auto status = luaL_loadstring(L, stream.str().c_str());
      if(status == LUA_ERRSYNTAX) {
        THALAMUS_LOG(warning) << "Syntax Error";
        auto error_str = lua_tostring(L, -1);
        ((*dict)["Error"]).assign(error_str);
        return;
      }
      ((*dict)["Error"]).assign("");
      THALAMUS_ASSERT(status == LUA_OK, "Lua Failure");

      lua_call(L, 0, 0);
      lua_getglobal(L, func_name.c_str());
      lua_copy(L, -1, index+1);
      lua_pop(L, 1);
    }

    std::map<long long, boost::signals2::scoped_connection> equation_connections;

    void on_equations_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto index = std::get<long long>(k);
      auto value = std::get<ObservableDictPtr>(v);
      equation_connections[index] = value->changed.connect(std::bind(&Impl::on_equation_change, this, value, index, _1, _2, _3));
      value->recap(std::bind(&Impl::on_equation_change, this, value, index, _1, _2, _3));
    }

    void on_change(ObservableCollection::Action, const ObservableCollection::Key& k, const ObservableCollection::Value& v) {
      auto key_str = std::get<std::string>(k);
      if(key_str == "Source") {
        auto value_str = std::get<std::string>(v);
        absl::StripAsciiWhitespace(&value_str);
        graph->get_node(value_str, [&](auto node) {
          current = this;
          auto locked = node.lock();
          if(!locked) {
            return;
          }
          source = std::dynamic_pointer_cast<AnalogNode>(locked).get();
          if(!source) {
            return;
          }
          channels_connection = source->channels_changed.connect([&] (auto) {
            channels_changed = true;
            outer->channels_changed(outer);
          });
          channels_changed = true;
          outer->channels_changed(outer);
          source_connection = locked->ready.connect([&] (auto) {
            if(!source->has_analog_data()) {
              return;
            }
            auto num_channels = source->num_channels();

            if(channels_changed) {
              channels_changed = false;
              channel_names.clear();
              sample_intervals.clear();
              for(auto i = 0;i < num_channels;++i) {
                auto name = source->name(i);
                channel_names.emplace_back(name.begin(), name.end());
                sample_intervals.push_back(source->sample_interval(i));
              }
              channel_names.push_back("Compute Time (ms)");
              sample_intervals.push_back(1s);
              if(equation_list) {
                auto num_equations = equation_list->size();
                if (num_equations) {
                  for (int i = num_equations - 1; i >= num_channels; --i) {
                    equation_list->erase(i);
                    --num_equations;
                  }
                }
                for(size_t i = 0;i < num_equations;++i) {
                  auto name = source->name(i);
                  ObservableDictPtr dict = equation_list->at(i);
                  (*dict)["Name"].assign(std::string(name.begin(), name.end()));
                }
                for(int i = num_equations;i < num_channels;++i) {
                  auto name = source->name(i);
                  auto dict = std::make_shared<ObservableDict>();
                  (*dict)["Equation"].assign("");
                  (*dict)["Error"].assign("");
                  (*dict)["Name"].assign(std::string(name.begin(), name.end()));
                  equation_list->at(i).assign(dict);
                }
              }
            }
            fill_stack(num_channels);
            data.resize(num_channels+1);
            last_data.resize(num_channels, 0);

            auto start = std::chrono::steady_clock::now();
            for(auto i = 0;i < num_channels;++i) {
              auto span = source->data(i);
              auto& transformed = data.at(i);
              transformed.assign(span.begin(), span.end());
              if(lua_isnil(L, i+1)) {
                continue;
              }

              int isnum = 0;
              sample_interval = source->sample_interval(i);
              if(unsafe) {
                for(auto j = 0ull;j < transformed.size();++j) {
                  sample_index = j;
                  auto& from = transformed.at(j);
                  last_data[i] = from;
                  lua_pushvalue(L, i+1);
                  lua_pushnumber(L, from);
                  lua_call(L, 1, 1);
                  from = lua_tonumber(L, -1);
                  lua_pop(L, 1);
                }
              } else {
                for(size_t j = 0;j < transformed.size();++j) {
                  sample_index = j;
                  auto& from = transformed.at(j);
                  last_data[i] = from;
                  lua_pushvalue(L, i+1);
                  lua_pushnumber(L, from);
                  auto status = lua_pcall(L, 1, 1, 0);
                  if (status == LUA_ERRRUN) {
                    auto error = lua_tostring(L, -1);
                    ObservableDictPtr dict = equation_list->at(i);
                    (*dict)["Error"].assign(error);
                    lua_pushnil(L);
                    lua_copy(L, -1, i+1);
                    lua_pop(L, 2);
                    break;
                  }
                  auto to = lua_tonumberx(L, -1, &isnum);
                  from = isnum ? to : 0;
                  lua_pop(L, 1);
                }
              }
            }
            compute_time += std::chrono::steady_clock::now() - start;
            did_compute = true;
            time = source->time();
            data.back().clear();
            outer->ready(outer);
          });
        });
      } else if (key_str == "Equations") {
        equation_list = std::get<ObservableListPtr>(v);
        equation_list->changed.connect(std::bind(&Impl::on_equations_change, this, _1, _2, _3));
        equation_list->recap(std::bind(&Impl::on_equations_change, this, _1, _2, _3));
      } else if (key_str == "Unsafe") {
        unsafe = std::get<bool>(v);
      }
    }
  };

  LuaNode::Impl* LuaNode::Impl::current = nullptr;

  LuaNode::LuaNode(ObservableDictPtr state, boost::asio::io_context& io_context, NodeGraph* graph)
    : impl(new Impl(state, io_context, graph, this)) {}

  LuaNode::~LuaNode() {}

  std::string LuaNode::type_name() {
    return "LUA";
  }

  std::chrono::nanoseconds LuaNode::time() const {
    return impl->time;
  }

  std::span<const double> LuaNode::data(int channel) const {
    if(static_cast<size_t>(channel) < impl->data.size()) {
      auto& data = impl->data.at(channel);
      return std::span<const double>(data.begin(), data.end());
    } else {
      return std::span<const double>();
    }
  }

  int LuaNode::num_channels() const {
    return impl->data.size();
  }

  std::string_view LuaNode::name(int channel) const {
    if(size_t(channel) < impl->channel_names.size()) {
      return impl->channel_names.at(channel);
    } else {
      return "";
    }
  }

  std::span<const std::string> LuaNode::get_recommended_channels() const {
    THALAMUS_ASSERT(false);
  }

  std::chrono::nanoseconds LuaNode::sample_interval(int channel) const {
    return impl->sample_intervals.at(channel);
  }

  void LuaNode::inject(const thalamus::vector<std::span<double const>>&, const thalamus::vector<std::chrono::nanoseconds>&, const thalamus::vector<std::string_view>&) {
    THALAMUS_ASSERT(false);
  }
   
  bool LuaNode::has_analog_data() const {
    return true;
  }

  boost::json::value LuaNode::process(const boost::json::value&) {
    return boost::json::value();
  }
}


