#include "timestamp.hh"
#include <ctime>
#include "exception.hh"

uint64_t timestamp_ns()
{
  timespec ts;
  CheckSystemCall("clock_gettime", clock_gettime(CLOCK_REALTIME, &ts));

  return ts.tv_sec * BILLION + ts.tv_nsec;
}

uint64_t timestamp_us()
{
  return timestamp_ns() / 1000;
}

uint64_t timestamp_ms()
{
  return timestamp_ns() / MILLION;
}

uint64_t timestamp_s()
{
  timespec ts;
  CheckSystemCall("clock_gettime", clock_gettime(CLOCK_REALTIME, &ts));

  return ts.tv_sec;
}
