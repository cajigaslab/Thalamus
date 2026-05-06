#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/asio.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <list>
#include <utility>

namespace thalamus {
using namespace std::chrono_literals;

struct Finally {
  std::function<void()> end;
  Finally(std::function<void()> _end) : end(_end) {}
  ~Finally() { end(); }
};

struct CoCondition {
  struct Waiter {
    boost::asio::io_context &io_context;
    boost::asio::steady_timer timer;
    std::function<bool()> condition;
  };
  boost::asio::io_context &io_context;
  std::list<Waiter *> waiters;
  CoCondition(boost::asio::io_context &_io_context) : io_context(_io_context) {}

  void notify() {
    for (auto waiter : waiters) {
      waiter->timer.cancel();
    }
  }

  template <typename T>
  boost::asio::awaitable<std::cv_status>
  wait(std::function<bool()> condition = {}, T timeout = 24h) {
    if (condition && condition()) {
      co_return std::cv_status::no_timeout;
    }
    Waiter waiter{io_context, boost::asio::steady_timer(io_context), condition};
    waiters.push_back(&waiter);
    auto i = waiters.end();
    --i;
    Finally f([&]() { waiters.erase(i); });
    waiter.timer.expires_after(timeout);
    do {
      auto [e] = co_await waiter.timer.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));
      if (e) {
        THALAMUS_ASSERT(e == boost::asio::error::operation_aborted,
                        "Wait error: %s", e.message());
        continue;
      }
      co_return std::cv_status::timeout;
    } while (condition && !condition());

    co_return std::cv_status::no_timeout;
  }

  boost::asio::awaitable<std::cv_status> wait() {
    std::cv_status result = co_await wait({}, 24h);
    co_return result;
  }

  template <std::invocable T>
  boost::asio::awaitable<std::cv_status> wait(T condition) {
    std::cv_status result = co_await wait(condition, 24h);
    co_return result;
  }

  template <typename T> boost::asio::awaitable<std::cv_status> wait(T timeout) {
    std::cv_status result = co_await wait({}, timeout);
    co_return result;
  }
};

template<typename T>
struct MovableClock : public T {
  static std::atomic_int64_t offset;
  static boost::signals2::signal<void()>* time_changed;

  template<typename TIME_POINT>
  static void set_offset(const TIME_POINT& new_offset) {
    offset = std::chrono::duration_cast<std::chrono::nanoseconds>(new_offset).count();
    THALAMUS_ASSERT(time_changed != nullptr, "MovableClock<T>::init was not called");
    (*time_changed)();
  }

  static typename T::duration get_offset() {
    return std::chrono::nanoseconds(offset);
  }

  template<typename DURATION>
  static void move(const DURATION& movement) {
    auto new_offset = offset + std::chrono::duration_cast<std::chrono::nanoseconds>(movement).count();
    set_offset(std::chrono::nanoseconds(new_offset));
  }

  static typename T::time_point now() {
    return T::now() + std::chrono::duration_cast<typename T::duration>(std::chrono::nanoseconds(offset));
  }

  static void init() {
    THALAMUS_ASSERT(time_changed == nullptr, "MovableClock<T>::init was already called");
    time_changed = new boost::signals2::signal<void()>();
  }

  static void cleanup() {
    THALAMUS_ASSERT(time_changed != nullptr, "MovableClock<T>::init was not called");
    delete time_changed;
    time_changed = nullptr;
  }
};

template<typename T>
std::atomic_int64_t MovableClock<T>::offset = 0;

template<typename T>
boost::signals2::signal<void()>* MovableClock<T>::time_changed = nullptr;

template<typename CLOCK>
struct MovableTimer : public boost::asio::basic_waitable_timer<CLOCK> {
  boost::signals2::scoped_connection connection;
  typename CLOCK::time_point deadline;
  std::function<void(boost::system::error_code)> callback;

  MovableTimer(boost::asio::io_context& ioc)
    : boost::asio::basic_waitable_timer<CLOCK>(ioc) {
    connection = CLOCK::time_changed->connect(std::bind(&MovableTimer<CLOCK>::on_time_changed, this));
  }

  void on_time_changed() {
    auto cancelled = expires_at(deadline);
    if(cancelled) {
      async_wait(callback);
    }
  }

  template<typename TIME>
  size_t expires_after(const TIME& new_deadline) {
    return expires_at(CLOCK::now() + new_deadline);
  }

  template<typename TIME>
  size_t expires_at(const TIME& new_deadline) {
    deadline = new_deadline;
    return boost::asio::basic_waitable_timer<CLOCK>::expires_at(deadline);
  }

  decltype(std::declval<boost::asio::basic_waitable_timer<CLOCK>>().async_wait(
      std::function<void(boost::system::error_code)>()))
  async_wait(const std::function<void(boost::system::error_code)>& handler) {
    callback = handler;
    return boost::asio::basic_waitable_timer<CLOCK>::async_wait(handler);
  }
};

template<typename T>
struct ClockGuard : Finally {
  ClockGuard() : Finally([original=T::get_offset()] { T::set_offset(original); }) {}
};

typedef MovableClock<std::chrono::steady_clock> MovableSteadyClock;
typedef MovableClock<std::chrono::system_clock> MovableSystemClock;
typedef MovableTimer<MovableSteadyClock> MovableSteadyTimer;
typedef MovableTimer<MovableSystemClock> MovableSystemTimer;
typedef ClockGuard<MovableSteadyClock> SteadyClockGuard;
typedef ClockGuard<MovableSteadyClock> SystemClockGuard;

inline void init_movable_clocks() {
  MovableSteadyClock::init();
  MovableSystemClock::init();
}

inline void cleanup_movable_clocks() {
  MovableSteadyClock::cleanup();
  MovableSystemClock::cleanup();
}

} // namespace thalamus
