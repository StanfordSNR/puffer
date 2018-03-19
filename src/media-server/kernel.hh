#ifndef KERNEL_HH
#define KERNEL_HH

#include <cstdint>

#include "address.hh"

using ConnInfo = struct ConnInfo {
  uint32_t cwnd;
  uint32_t rtt;
};

ConnInfo get_conn_info(const Address & local_addr, const Address & peer_addr);

int set_conn_cwnd(const Address & local_addr, const Address & peer_addr,
                  const uint32_t new_cwnd);

#endif /* KERNEL_HH */