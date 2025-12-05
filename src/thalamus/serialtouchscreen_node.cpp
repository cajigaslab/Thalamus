#include <modalities_util.hpp>
#include <thalamus/serialtouchscreen_node.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/asio.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
struct SerialTouchScreenNode::Impl {
  ObservableDictPtr state;
  boost::asio::io_context& io_context;
  boost::signals2::scoped_connection state_connection;
  SerialTouchScreenNode *outer;
  NodeGraph *graph;
  std::string port_name;
  boost::asio::serial_port port;
  bool running = false;
  size_t position = 0;
  unsigned char buffer[32];
  double x;
  double y;
  std::chrono::nanoseconds time;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context & _io_context, NodeGraph *_graph,
       SerialTouchScreenNode *_outer)
      : state(_state), io_context(_io_context), outer(_outer), graph(_graph), port(io_context) {

    //serial_port.set_option(boost::asio::serial_port_base::baud_rate(9600));
    //serial_port.set_option(boost::asio::serial_port_base::character_size(8));
    //serial_port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
    //serial_port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    //serial_port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));

    state_connection = state->recursive_changed.connect(
        std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  void on_read(const boost::system::error_code& error, size_t) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    THALAMUS_ASSERT(!error, "Unexpected error");

    if(!running) {
      return;
    }

    port.async_read_some(boost::asio::buffer(buffer + position, sizeof(buffer) - position),
                         std::bind(&Impl::on_read, this, _1, _2));
  }

  void on_change(ObservableCollection *, ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Port") {
      port_name = std::get<std::string>(v);
    } else if (key_str == "Running") {
      running = std::get<bool>(v);
      if(port.is_open()) {
        port.close();
      }
      port.open(port_name);
      port.async_read_some(boost::asio::buffer(buffer + position, sizeof(buffer) - position),
                           std::bind(&Impl::on_read, this, _1, _2));
    }
  }
};

SerialTouchScreenNode::SerialTouchScreenNode(ObservableDictPtr state,
                                 boost::asio::io_context &io_context,
                                 NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

SerialTouchScreenNode::~SerialTouchScreenNode() {}

std::string SerialTouchScreenNode::type_name() { return "SERIAL_TOUCH_SCREEN"; }

std::chrono::nanoseconds SerialTouchScreenNode::time() const {
  return impl->time;
}

std::span<const double> SerialTouchScreenNode::data(int channel) const {
  if (channel == 0) {
    return std::span<const double>(&impl->x, &impl->x + 1);
  } else if (channel == 1) {
    return std::span<const double>(&impl->y, &impl->y + 1);
  } else {
    return std::span<const double>();
  }
}

int SerialTouchScreenNode::num_channels() const { return 2; }

std::string_view SerialTouchScreenNode::name(int channel) const {
  if (channel == 0) {
    return "X";
  } else if (channel == 1) {
    return "Y";
  } else {
    return "";
  }
}

std::chrono::nanoseconds SerialTouchScreenNode::sample_interval(int) const {
  return 0ns;
}

void SerialTouchScreenNode::inject(const thalamus::vector<std::span<double const>> &,
                             const thalamus::vector<std::chrono::nanoseconds> &,
                             const thalamus::vector<std::string_view> &) {
  THALAMUS_ASSERT(false, "Unimplemented");
}

bool SerialTouchScreenNode::has_analog_data() const { return true; }

size_t SerialTouchScreenNode::modalities() const {
  return infer_modalities<SerialTouchScreenNode>();
}
} // namespace thalamus
