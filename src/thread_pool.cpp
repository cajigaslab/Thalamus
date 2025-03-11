#include <modalities_util.hpp>
#include <thalamus/thread.hpp>
#include <thalamus/tracing.hpp>
#include <thread_pool.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/pool/object_pool.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::chrono_literals;

void ThreadPool::thread_target(std::string thread_name) {
  set_current_thread_name(thread_name);
  while (true) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(mutex);
      --num_busy_threads;
      condition.wait(lock, [&]() { return !running || !jobs.empty(); });
      ++num_busy_threads;
      if (!running) {
        break;
      }
      job = std::move(jobs.front());
      jobs.pop_front();
    }

    TRACE_EVENT("thalamus", "ThreadPool::thread_target");
    job();
  }
}

struct ThreadPoolNode::Impl {
  boost::asio::io_context &io_context;
  ObservableDictPtr state;
  boost::signals2::scoped_connection state_connection;
  boost::signals2::scoped_connection options_connection;
  boost::signals2::scoped_connection source_connection;
  bool is_running = false;
  ThreadPoolNode *outer;
  std::chrono::nanoseconds time;
  std::thread oculomatic_thread;
  bool running = true;
  bool computing = false;
  thalamus_grpc::Image image;
  NodeGraph *graph;
  size_t threshold;
  size_t min_area;
  size_t max_area;
  double x_gain;
  double y_gain;
  bool invert_x;
  bool invert_y;
  size_t next_input_frame = 0;
  size_t next_output_frame = 0;
  std::chrono::nanoseconds _time;
  std::chrono::steady_clock::time_point last_time;
  std::chrono::steady_clock::time_point _start_time;
  thalamus::vector<double> buffer;

  ThreadPool &pool;
  boost::asio::steady_timer timer;
  AnalogNodeImpl analog_impl;

  Impl(ObservableDictPtr _state, boost::asio::io_context &_io_context,
       ThreadPoolNode *_outer, NodeGraph *_graph)
      : io_context(_io_context), state(_state), outer(_outer), graph(_graph),
        pool(_graph->get_thread_pool()), timer(_io_context) {
    using namespace std::placeholders;
    analog_impl.inject({{std::span<double const>()}}, {0ns}, {});

    analog_impl.ready.connect([_outer](Node *) { _outer->ready(_outer); });

    state_connection =
        state->changed.connect(std::bind(&Impl::on_change, this, _1, _2, _3));
    this->state->recap(std::bind(&Impl::on_change, this, _1, _2, _3));

    last_time = std::chrono::steady_clock::now();
    _start_time = last_time;
    _time = 0ns;
    on_timer(boost::system::error_code());
  }

  ~Impl() {
    (*state)["Running"].assign(false, [&] {});
  }

  void on_timer(const boost::system::error_code &error) {
    TRACE_EVENT("thalamus", "ThreadPool::on_timer");
    if (error.value() == boost::asio::error::operation_aborted) {
      return;
    }
    BOOST_ASSERT(!error);
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - _start_time);
    last_time = now;
    buffer.clear();

    auto new_time = _time;
    while (new_time <= elapsed) {
      buffer.push_back(pool.idle());
      new_time += 32ms;
    }
    analog_impl.inject({{buffer.begin(), buffer.end()}}, {32ms},
                       {"Idle Threads"}, now.time_since_epoch());
    _time = new_time;
    // auto after = std::chrono::steady_clock::now();
    // std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(after
    // - now).count() << std::endl;
    if (!running) {
      return;
    }
    timer.expires_after(32ms);
    timer.async_wait(std::bind(&Impl::on_timer, this, _1));
  }

  void on_change(ObservableCollection::Action,
                 const ObservableCollection::Key &k,
                 const ObservableCollection::Value &) {
    auto key_str = std::get<std::string>(k);
  }
};

ThreadPoolNode::ThreadPoolNode(ObservableDictPtr state,
                               boost::asio::io_context &io_context,
                               NodeGraph *graph)
    : impl(new Impl(state, io_context, this, graph)) {}

ThreadPoolNode::~ThreadPoolNode() {}

std::string ThreadPoolNode::type_name() { return "THREAD_POOL"; }

std::chrono::nanoseconds ThreadPoolNode::time() const {
  return impl->analog_impl.time();
}

std::span<const double> ThreadPoolNode::data(int index) const {
  return impl->analog_impl.data(index);
}

int ThreadPoolNode::num_channels() const {
  return impl->analog_impl.num_channels();
}

std::chrono::nanoseconds ThreadPoolNode::sample_interval(int channel) const {
  return impl->analog_impl.sample_interval(channel);
}

std::string_view ThreadPoolNode::name(int channel) const {
  switch (channel) {
  case 0:
    return "Idle Threads";
  default:
    return "";
  }
}

void ThreadPoolNode::inject(
    const thalamus::vector<std::span<double const>> &data,
    const thalamus::vector<std::chrono::nanoseconds> &interval,
    const thalamus::vector<std::string_view> &names) {
  impl->analog_impl.inject(data, interval, names);
}

bool ThreadPoolNode::has_analog_data() const { return true; }

boost::json::value ThreadPoolNode::process(const boost::json::value &) {
  return boost::json::value();
}

size_t ThreadPoolNode::modalities() const {
  return infer_modalities<ThreadPoolNode>();
}
} // namespace thalamus
