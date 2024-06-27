#include <cstdint>

class IClock
{
public:
  virtual ~IClock() = default;
  virtual int64_t GetEpochMs() const = 0;
  virtual int64_t GetEpochUs() const = 0;
};