#include <wallclock_node.hpp>
#include <modalities_util.hpp>

using namespace thalamus;

struct WallClockNode::Impl {
  ObservableDictPtr state;
  boost::asio::steady_timer timer;
  WallClockNode* outer;

  std::chrono::steady_clock::duration steady_time;
  std::chrono::system_clock::duration system_time;
  double system_time_double;

  Impl(ObservableDictPtr _state, boost::asio::io_context &io_context,
       NodeGraph *, WallClockNode *_outer)
      : state(_state), timer(io_context), outer(_outer) {

    timer.expires_after(1s);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  void on_timer(const boost::system::error_code &error) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);

    steady_time = std::chrono::steady_clock::now().time_since_epoch();
    system_time = std::chrono::system_clock::now().time_since_epoch();
    system_time_double = double(std::chrono::duration_cast<std::chrono::nanoseconds>(system_time).count());
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
