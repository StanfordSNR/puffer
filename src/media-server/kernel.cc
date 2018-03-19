#include "kernel.hh"

#include <cassert>
#include <string>
#include <iostream>
#include <sstream>
#include <ios>
#include <iomanip>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <endian.h>

using namespace std;

const string tv_proc_prefix = "/proc/tv-cong-";

string proc_fname(const Address & local_addr, const Address & peer_addr)
{
  struct sockaddr peer_sa, local_sa;
  local_sa = local_addr.to_sockaddr();
  peer_sa = peer_addr.to_sockaddr();

  assert (local_sa.sa_family == AF_INET);
  assert (peer_sa.sa_family == AF_INET);

  uint32_t local_v4;
  uint32_t peer_v4;
  inet_pton(AF_INET, local_addr.ip().c_str(), &local_v4);
  inet_pton(AF_INET, peer_addr.ip().c_str(), &peer_v4);

  /* format names in network endian */
  ostringstream ss;
  ss << tv_proc_prefix;
  ss << nouppercase << setfill('0') << setw(8) << hex << htobe32(local_v4)
     << ':';
  ss << nouppercase << setfill('0') << setw(4) << hex
     << htobe16(local_addr.port()) << ':';
  ss << nouppercase << setfill('0') << setw(8) << hex << htobe32(peer_v4)
     << ':';
  ss << nouppercase << setfill('0') << setw(4) << hex
     << htobe16(peer_addr.port());
  return ss.str();
}

ConnInfo get_conn_info(const Address & local_addr, const Address & peer_addr)
{
  cerr << "proc read: " <<  proc_fname(local_addr, peer_addr) << endl;
  // TODO: actually read some data
  return { 0, 0 };
}

int set_conn_cwnd(const Address & local_addr, const Address & peer_addr,
                   const uint32_t new_cwnd)
{
  cerr << "proc write: " <<  proc_fname(local_addr, peer_addr) << endl;
  assert(new_cwnd != 0);
  // TODO: actually write some data
  return 0;
}