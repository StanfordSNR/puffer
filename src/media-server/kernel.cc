#include "kernel.hh"

#include <cassert>

ConnInfo get_conn_info(const Address & peer_addr, const Address & local_addr)
{
  // TODO: do something useful here
  assert (peer_addr.str() != "");
  assert (local_addr.str() != "");
  return { 0, 0, 0 };
}

bool set_conn_cwnd(const Address & peer_addr, const Address & local_addr,
                   const uint32_t new_cwnd)
{
  // TODO: do something useful here
  assert (peer_addr.str() != "");
  assert (local_addr.str() != "");
  assert (new_cwnd > 0);
  return true;
}