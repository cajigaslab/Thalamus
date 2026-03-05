#include <limits>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <regex>
#include <modalities_util.hpp>
#include <thalamus/joystick_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/asio.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
struct JoystickNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context &io_context;
  boost::asio::steady_timer timer;
  boost::signals2::scoped_connection state_connection;
  JoystickNode *outer;
  NodeGraph *graph;
  std::string port_name;
  boost::asio::serial_port port;
  boost::asio::streambuf line_buffer;
  bool running = false;

  bool invert_x = false;
  bool invert_y = false;
  double x_center = 512.0;
  double y_center = 512.0;
  double dead_zone = 3.0;

  double x = 0.0;
  double y = 0.0;
  double frequency = 0.0;
  std::chrono::nanoseconds time = std::chrono::nanoseconds(0);
  std::chrono::nanoseconds sample_interval = std::chrono::milliseconds(5);
  static constexpr unsigned int baud_rate = 115200;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context, NodeGraph *_graph,
       JoystickNode *_outer)
      : state(_state), io_context(_io_context), timer(_io_context), outer(_outer), graph(_graph), port(io_context) {
    state_connection = state->recursive_changed.connect(
        std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  static int clamp_adc(int v) {
    return std::max(0, std::min(1023, v));
  }

  double normalize_axis(int value, double center) const {
    if (std::abs(double(value) - center) <= dead_zone) {
      value = int(center);
    }
    value = clamp_adc(value);
    return (double(value) - center) / 512.0;
  }

  static bool parse_line(const std::string &line, int &raw_x, int &raw_y) {
    if (sscanf(line.c_str(), "%d,%d", &raw_x, &raw_y) == 2) {
      return true;
    }

    static const std::regex labeled_pattern(R"(x\s*=\s*(\d+)\s*,\s*y\s*=\s*(\d+))",
                                            std::regex::icase);
    std::smatch match;
    if (std::regex_search(line, match, labeled_pattern) && match.size() >= 3) {
      raw_x = std::stoi(match[1].str());
      raw_y = std::stoi(match[2].str());
      return true;
    }

    return false;
  }

  void on_timer(const boost::system::error_code &error) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    THALAMUS_ASSERT(!error, "Unexpected error");

    if (!running) {
      return;
    }

    outer->ready(outer);
    timer.expires_after(sample_interval);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  void start_read() {
    if (!running || !port.is_open()) {
      return;
    }
    boost::asio::async_read_until(
        port, line_buffer, '\n',
        std::bind(&Impl::on_read, this, std::placeholders::_1, std::placeholders::_2));
  }

  void on_read(const boost::system::error_code &error, size_t) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    if (error) {
      THALAMUS_LOG(warning) << "Joystick serial read error: " << error.message();
      return;
    }
    if (!running) {
      return;
    }

    std::istream stream(&line_buffer);
    std::string line;
    std::getline(stream, line);

    int raw_x = 0;
    int raw_y = 0;
    if (parse_line(line, raw_x, raw_y)) {
      auto normalized_x = normalize_axis(raw_x, x_center);
      auto normalized_y = normalize_axis(raw_y, y_center);
      x = invert_x ? -normalized_x : normalized_x;
      y = invert_y ? -normalized_y : normalized_y;

      auto new_time = std::chrono::steady_clock::now().time_since_epoch();
      if (time.count() > 0 && new_time > time) {
        frequency = 1e9 / double((new_time - time).count());
      }
      time = new_time;
    }

    start_read();
  }

  void stop_io() {
    timer.cancel();
    if (port.is_open()) {
      boost::system::error_code ec;
      port.cancel(ec);
      port.close(ec);
    }
  }

  void start_io() {
    stop_io();

    boost::system::error_code ec;
    port.open(port_name, ec);
    if (ec) {
      thalamus_grpc::Dialog dialog;
      dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
      dialog.set_title(std::string("Joystick Error"));
      dialog.set_message(ec.message());
      graph->dialog(dialog);
      (*state)["Running"].assign(false);
      return;
    }

    port.set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
    line_buffer.consume(line_buffer.size());
    timer.expires_after(sample_interval);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
    start_read();
  }

  void on_change(ObservableCollection *, ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Port") {
      port_name = std::get<std::string>(v);
    } else if (key_str == "Invert X") {
      invert_x = std::get<bool>(v);
    } else if (key_str == "Invert Y") {
      invert_y = std::get<bool>(v);
    } else if (key_str == "X Center") {
      x_center = std::holds_alternative<long long>(v) ? double(std::get<long long>(v))
                                                       : std::get<double>(v);
    } else if (key_str == "Y Center") {
      y_center = std::holds_alternative<long long>(v) ? double(std::get<long long>(v))
                                                       : std::get<double>(v);
    } else if (key_str == "Dead Zone") {
      dead_zone = std::holds_alternative<long long>(v) ? double(std::get<long long>(v))
                                                        : std::get<double>(v);
    } else if (key_str == "Running") {
      running = std::get<bool>(v);
      if (running) {
        start_io();
      } else {
        stop_io();
      }
    }
  }
};

JoystickNode::JoystickNode(ObservableDictPtr state, boost::asio::io_context &io_context,
                           NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

JoystickNode::~JoystickNode() {}

std::string JoystickNode::type_name() { return "JOYSTICK"; }

std::chrono::nanoseconds JoystickNode::time() const { return impl->time; }

std::span<const double> JoystickNode::data(int channel) const {
  if (channel == 0) {
    return std::span<const double>(&impl->x, &impl->x + 1);
  } else if (channel == 1) {
    return std::span<const double>(&impl->y, &impl->y + 1);
  } else if (channel == 2) {
    return std::span<const double>(&impl->frequency, &impl->frequency + 1);
  }
  return std::span<const double>();
}

int JoystickNode::num_channels() const { return 3; }

std::string_view JoystickNode::name(int channel) const {
  if (channel == 0) {
    return "X";
  } else if (channel == 1) {
    return "Y";
  } else if (channel == 2) {
    return "Frequency";
  }
  return "";
}

std::chrono::nanoseconds JoystickNode::sample_interval(int) const {
  return impl->sample_interval;
}

void JoystickNode::inject(const thalamus::vector<std::span<double const>> &,
                          const thalamus::vector<std::chrono::nanoseconds> &,
                          const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

bool JoystickNode::has_analog_data() const { return true; }

size_t JoystickNode::modalities() const {
  return infer_modalities<JoystickNode>();
}
} // namespace thalamus
