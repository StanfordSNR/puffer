#include "timestamp.hh"

#include "exception.hh"

/* nanoseconds per millisecond */
static const uint64_t MILLION = 1000000;

/* nanoseconds per second */
static const uint64_t BILLION = 1000 * MILLION;

uint64_t timestamp_ms()
{
  timespec ts;
  CheckSystemCall("clock_gettime", clock_gettime(CLOCK_REALTIME, &ts));

  const uint64_t nanos = ts.tv_sec * BILLION + ts.tv_nsec;
  return nanos / MILLION;
}
