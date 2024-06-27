#include <tracing/systemclock.h>
#include <chrono>
#include <exception>

int64_t SystemClock::GetEpochMs() const
{
  auto epoch = std::chrono::high_resolution_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
}

int64_t SystemClock::GetEpochUs() const
{
  auto epoch = std::chrono::high_resolution_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
}