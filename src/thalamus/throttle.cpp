#include <thalamus/throttle.hpp>
#include <algorithm>

using namespace thalamus;
using namespace std::chrono_literals;

bool Throttle::update(std::chrono::nanoseconds now, double rate) {
  if (rate > 0) {
    while (!times.empty() && now - times.front() >= 1s) {
      std::pop_heap(times.begin(), times.end(),
                    [](auto &l, auto &r) { return l > r; });
      times.pop_back();
    }
    if (!times.empty()) {
      auto duration = now - times.front();
      auto duration_seconds = double(duration.count()) / decltype(duration)::period::den;
      if (double(times.size()) / duration_seconds >= rate) {
        return false;
      }
    }

    times.push_back(now);
    std::push_heap(times.begin(), times.end(),
                   [](auto &l, auto &r) { return l > r; });
  }
  return true;
}
