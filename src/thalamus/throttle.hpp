#pragma once
#include <vector>
#include <chrono>

namespace thalamus {
class Throttle {
  std::vector<std::chrono::nanoseconds> times;
public:
  bool update(std::chrono::nanoseconds now, double rate);
};
}
