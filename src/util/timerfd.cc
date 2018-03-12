#include "timerfd.hh"
#include "exception.hh"

using namespace std;

Timerfd::Timerfd(int clockid, int flags)
  : FileDescriptor(CheckSystemCall("timerfd_create",
                                   timerfd_create(clockid, flags)))
{}

void Timerfd::start(int first_exp_ms, int interval_ms)
{
  time_t first_exp_s = first_exp_ms / 1000;
  long first_exp_ns = (first_exp_ms - first_exp_s * 1000) * 1000000;

  time_t interval_s = interval_ms / 1000;
  long interval_ns = (interval_ms - interval_s * 1000) * 1000000;

  struct itimerspec ts;
  ts.it_value.tv_sec = first_exp_s;
  ts.it_value.tv_nsec = first_exp_ns;
  ts.it_interval.tv_sec = interval_s;
  ts.it_interval.tv_nsec = interval_ns;

  CheckSystemCall("timerfd_settime",
                  timerfd_settime(fd_num(), 0, &ts, nullptr));
}

uint64_t Timerfd::expirations()
{
  uint64_t num_exp = 0;

  int r = CheckSystemCall("read", ::read(fd_num(), &num_exp, sizeof(num_exp)));
  if (r != sizeof(num_exp)) {
    throw runtime_error(
        "Timerfd::expirations() returns a wrong number of expirations");
  }

  register_read();

  return num_exp;
}
