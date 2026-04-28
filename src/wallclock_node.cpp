#include <wallclock_node.hpp>
#include <modalities_util.hpp>
#include <thalamus/async.hpp>

using namespace thalamus;

struct WallClockNode::Impl {
  ObservableDictPtr state;
  MovableSteadyTimer timer;
  WallClockNode* outer;

  std::chrono::steady_clock::duration steady_time;
  std::chrono::system_clock::duration system_time;
  double system_time_double;
  uint64_t system_time_uint64;
  bool is_uint64 = false;
  boost::signals2::scoped_connection state_connection;

  Impl(ObservableDictPtr _state, boost::asio::io_context &io_context,
       NodeGraph *, WallClockNode *_outer)
      : state(_state), timer(io_context), outer(_outer) {

    timer.expires_after(1s);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));
  }

  
  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &key,
                 const ObservableCollection::Value &value) {
    auto str_key = std::get<std::string>(key);
    if(str_key == "Integer Data") {
      is_uint64 = std::get<bool>(value);
    }
  }

  void on_timer(const boost::system::error_code &error) {
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    THALAMUS_ASSERT(!error, "Unexpected error");

    steady_time = MovableSteadyClock::now().time_since_epoch();
    system_time = MovableSystemClock::now().time_since_epoch();
    system_time_uint64 = std::chrono::duration_cast<std::chrono::nanoseconds>(system_time).count();
    system_time_double = double(system_time_uint64);
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

std::span<const uint64_t> WallClockNode::uint64_data(int index) const {
  return std::span<const uint64_t>(&impl->system_time_uint64, &impl->system_time_uint64+1);
}
bool WallClockNode::is_uint64_data() const {
  return impl->is_uint64;
}