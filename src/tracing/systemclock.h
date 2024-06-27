#include <tracing/iclock.h>

class SystemClock : public IClock
{
public:
  int64_t GetEpochMs() const override;
  int64_t GetEpochUs() const override;
};