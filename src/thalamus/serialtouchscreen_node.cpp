#include <limits>
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
  boost::asio::steady_timer timer;
  boost::signals2::scoped_connection state_connection;
  SerialTouchScreenNode *outer;
  NodeGraph *graph;
  std::string port_name;
  boost::asio::serial_port port;
  bool running = false;
  size_t position = 0;
  unsigned char buffer[32];
  double x = std::numeric_limits<unsigned short>::min();
  double y = std::numeric_limits<unsigned short>::min();
  double status;
  std::chrono::nanoseconds time;
  double repeat_count = 0;
  double frequency = 0;

  std::chrono::nanoseconds sample_interval = 20ms;
  std::chrono::nanoseconds no_touch_timeout = 30ms;

  enum class State {
    DELIMITER1,
    DELIMITER2,
    STATUS,
    XL,
    XH,
    YL,
    YH,
    TID,
    RES,
    CHK
  };
  State read_state = State::DELIMITER1;
  unsigned char xl;
  unsigned char xh;
  unsigned char yl;
  unsigned char yh;

public:
  Impl(ObservableDictPtr _state, boost::asio::io_context & _io_context, NodeGraph *_graph,
       SerialTouchScreenNode *_outer)
      : state(_state), io_context(_io_context), timer(_io_context), outer(_outer), graph(_graph), port(io_context) {

    //serial_port.set_option(boost::asio::serial_port_base::character_size(8));
    //serial_port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
    //serial_port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    //serial_port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));

    state_connection = state->recursive_changed.connect(
        std::bind(&Impl::on_change, this, _1, _2, _3, _4));
    state->recap(std::bind(&Impl::on_change, this, state.get(), _1, _2, _3));
  }

  void on_timer(const boost::system::error_code& error) {
    if (error.value() == boost::asio::error::operation_aborted) {
      THALAMUS_LOG(info) << "TOUCH ABORTED";
      return;
    }
    THALAMUS_ASSERT(!error, "Unexpected error");

    if(!running) {
      return;
    }

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    if(now - time > no_touch_timeout) {
      x = std::numeric_limits<unsigned short>::min();
      y = std::numeric_limits<unsigned short>::min();
    }
    outer->ready(outer);

    ++repeat_count;

    auto elapsed = std::chrono::steady_clock::now().time_since_epoch() - now;
    auto interval = elapsed > sample_interval ? 0ns : sample_interval - elapsed;

    timer.expires_after(interval);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  void on_read(const boost::system::error_code& error, size_t bytes_transferred) {
    if (error.value() == boost::asio::error::operation_aborted) {
      THALAMUS_LOG(info) << "TOUCH ABORTED";
      return;
    }
    THALAMUS_ASSERT(!error, "Unexpected error");

    if(!running) {
      return;
    }

    //THALAMUS_LOG(info) << bytes_transferred;

    for(size_t i = 0;i < bytes_transferred;++i) {
      switch(read_state) {
        case State::DELIMITER1:
          if(buffer[i] == 0x55) {
            read_state = State::DELIMITER2;
          }
          break;
        case State::DELIMITER2:
          if(buffer[i] == 0x54) {
            read_state = State::STATUS;
          } else {
            read_state = State::DELIMITER1;
          }
          break;
        case State::STATUS:
          read_state = State::XL;
          break;
        case State::XL:
          xl = buffer[i];
          read_state = State::XH;
          break;
        case State::XH:
          xh = buffer[i];
          read_state = State::YL;
          break;
        case State::YL:
          yl = buffer[i];
          read_state = State::YH;
          break;
        case State::YH:
          yh = buffer[i];
          read_state = State::TID;
          break;
        case State::TID:
          read_state = State::RES;
          break;
        case State::RES:
          read_state = State::CHK;
          break;
        case State::CHK:
          x = (xh << 8) | xl;
          y = (yh << 8) | yl;
          auto new_time = std::chrono::steady_clock::now().time_since_epoch();
          frequency = 1e9/double((new_time - time).count());
          time = new_time;
          read_state = State::DELIMITER1;
          break;
      }
    }

    port.async_read_some(boost::asio::buffer(buffer, sizeof(buffer)),
                     std::bind(&Impl::on_read, this, _1, _2));
  }

  void on_change(ObservableCollection *, ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    if (key_str == "Port") {
      port_name = std::get<std::string>(v);
    } else if (key_str == "No Touch Timeout (ms)") {
      no_touch_timeout = std::chrono::milliseconds(std::get<long long>(v));
    } else if (key_str == "Running") {
      running = std::get<bool>(v);
      if(port.is_open()) {
        port.close();
        timer.cancel();
      }
      if(running) {
        boost::system::error_code ec;
        port.open(port_name, ec);
        if (ec) {
          thalamus_grpc::Dialog dialog;
          dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
          dialog.set_title(std::string("Touch Screen Error"));
          dialog.set_message(ec.message());
          graph->dialog(dialog);
          (*state)["Running"].assign(false);
          return;
        }
        THALAMUS_LOG(info) << "Start Reading " << position;
        port.set_option(boost::asio::serial_port_base::baud_rate(19200));
        read_state = State::DELIMITER1;
        port.async_read_some(boost::asio::buffer(buffer, sizeof(buffer)),
                         std::bind(&Impl::on_read, this, _1, _2));
        timer.expires_after(sample_interval);
        timer.async_wait(std::bind(&Impl::on_timer, this, _1));
      }
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
  } else if (channel == 2) {
    return std::span<const double>(&impl->frequency, &impl->frequency + 1);
  } else {
    return std::span<const double>();
  }
}

int SerialTouchScreenNode::num_channels() const { return 3; }

std::string_view SerialTouchScreenNode::name(int channel) const {
  if (channel == 0) {
    return "X";
  } else if (channel == 1) {
    return "Y";
  } else if (channel == 2) {
    return "Frequency";
  } else {
    return "";
  }
}

std::chrono::nanoseconds SerialTouchScreenNode::sample_interval(int) const {
  return impl->sample_interval;
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
