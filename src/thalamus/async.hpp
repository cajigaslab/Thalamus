#pragma once

#include <boost/asio.hpp>
#include <list>

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
} // namespace thalamus
