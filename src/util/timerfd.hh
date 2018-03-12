#ifndef TIMERFD_HH
#define TIMERFD_HH

#include <sys/timerfd.h>
#include "file_descriptor.hh"

class Timerfd : public FileDescriptor
{
public:
  Timerfd(int clockid = CLOCK_MONOTONIC, int flags = TFD_NONBLOCK);

  void start(int first_expiration_ms, int interval_ms = 0);

  uint64_t expirations();
};

#endif /* TIMERFD_HH */
