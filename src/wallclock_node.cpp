#include <wallclock_node.hpp>
#include <modalities_util.hpp>
#include <thalamus/async.hpp>
#include <fcntl.h>

using namespace thalamus;

struct WallClockNode::Impl {
  ObservableDictPtr state;
  MovableSteadyTimer timer;
  WallClockNode* outer;

  std::chrono::steady_clock::duration steady_time;
  std::chrono::system_clock::duration system_time;
  double system_time_double;
  uint64_t system_time_ulong;

  int fd = -1;
#ifndef _WIN32
  clockid_t clkid;
  struct ntptimeval ntv;
  struct timespec ts;
#endif

  enum class Type {
    System,
    NTP,
    PTP
  };
  Type type = Type::System;
  bool integer_values = false;

  Impl(ObservableDictPtr _state, boost::asio::io_context &io_context,
       NodeGraph *, WallClockNode *_outer)
      : state(_state), timer(io_context), outer(_outer) {

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));

    timer.expires_after(1s);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &v) {
    auto key_str = std::get<std::string>(k);
    outer->channels_changed(outer);
    if (key_str == "Type") {
      auto val_str = std::get<std::string>(v);
      if(fd >= 0) {
        close(fd);
        fd = -1;
      }

      if(val_str == "System") {
        type = Type::System;
      } else {
#ifdef _WIN32
        thalamus_grpc::Dialog dialog;
        dialog.set_type(thalamus_grpc::Dialog::Type::Dialog_Type_ERROR);
        dialog.set_title("Unsupported clock type");
        dialog.set_message("Windows only supports System clock.");
        graph->dialog(dialog);
        (*state)["Type"].assign("System");
#else
        if(val_str == "NTP") {
          type = Type::NTP;
        } else if(val_str == "PTP") {
          type = Type::PTP;
          fd = open("/dev/ptp0", O_RDONLY);
          clkid = clockid_t((uint32_t(~fd) << 3) | 3);
        }
#endif
      }
    } else if (key_str == "Integer Values") {
      integer_values = std::get<bool>(v);
    }
  }

  void on_timer(const boost::system::error_code &error) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    THALAMUS_ASSERT(!error, "Unexpected error");

    steady_time = MovableSteadyClock::now().time_since_epoch();

    switch(type) {
      case Type::System:
        system_time = MovableSystemClock::now().time_since_epoch();
        break;
#ifndef _WIN32
      case Type::NTP: {
        ntp_gettime(&ntv);
        system_time = std::chrono::seconds(ntv.time.tv_sec) + std::chrono::microseconds(ntv.time.tv_usec);
        break;
      }
      case Type::PTP: {
        clock_gettime(clkid, &ts);
        system_time = std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
        break;
      }
#endif
      default:
        system_time = 0ns;
    }
    system_time_ulong = std::chrono::duration_cast<std::chrono::nanoseconds>(system_time).count();
    system_time_double = double(system_time_ulong);
    outer->ready(outer);
    timer.expires_after(1s);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }
};

WallClockNode::WallClockNode(ObservableDictPtr state,
                                     boost::asio::io_context &io_context,
                                     NodeGraph *graph)
    : impl(new Impl(state, io_context, graph, this)) {}

std::string WallClockNode::type_name() { return "WALLCLOCK"; }

std::string_view WallClockNode::name(int) const {
  return "Epoch (ns)";
}

std::span<const double> WallClockNode::data(int) const {
  return std::span<const double>(&impl->system_time_double, &impl->system_time_double+1);
}
  
std::span<const uint64_t> WallClockNode::ulong_data(int) const {
  return std::span<const double>(&impl->system_time_ulong, &impl->system_time_ulong+1);
}

bool WallClockNode::is_ulong_data() const {
  return impl->integer_values;
}

int WallClockNode::num_channels() const {
  return 1;
}

void WallClockNode::inject(
    const thalamus::vector<std::span<double const>> &data,
    const thalamus::vector<std::chrono::nanoseconds> &,
    const thalamus::vector<std::string_view> &) {
  impl->system_time_double = data[0][0];
  ready(this);
}

std::chrono::nanoseconds WallClockNode::sample_interval(int) const {
  return 1s;
}
std::chrono::nanoseconds WallClockNode::time() const {
  return impl->steady_time;
}

size_t WallClockNode::modalities() const {
  return infer_modalities<WallClockNode>();
}
