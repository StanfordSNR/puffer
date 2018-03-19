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
#include <fcntl.h>

#include "file_descriptor.hh"

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

optional<ConnInfo> get_conn_info(const Address & local_addr,
                                 const Address & peer_addr)
{
  string proc_file = proc_fname(local_addr, peer_addr);
  cerr << "proc read: " << proc_file << endl;

  int fd = open(proc_file.c_str(), O_RDONLY);
  if (fd == -1) {
    cerr << "could not open for reading: " << proc_file << endl;
    return nullopt;
  }

  // TODO: probably want to catch something here
  string data;
  try {
    data = FileDescriptor(fd).read_exactly(2 * sizeof(uint32_t));
  } catch (const runtime_error & e) {
    cerr << "read failed: " << proc_file << endl;
    return nullopt;
  }

  uint32_t cwnd = *reinterpret_cast<uint32_t*>(&data[0] + sizeof(uint32_t));
  uint32_t rtt = *reinterpret_cast<uint32_t*>(&data[0] + sizeof(uint32_t));
  return optional<ConnInfo>({cwnd, rtt});
}

int set_conn_cwnd(const Address & local_addr, const Address & peer_addr,
                  const uint32_t new_cwnd)
{
  string proc_file = proc_fname(local_addr, peer_addr);
  cerr << "proc write: " << proc_file << endl;

  int fd = open(proc_file.c_str(), O_WRONLY);
  if (fd == -1) {
    cerr << "could not open for writing: " << proc_file << endl;
    return -1;
  }

  uint32_t cwnd_copy = new_cwnd;
  try {
    FileDescriptor(fd).write(
      {reinterpret_cast<char *>(&cwnd_copy), sizeof(uint32_t)}, true);
  } catch (const runtime_error & e) {
    cerr << "write failed: " << proc_file << endl;
    return -1;
  }

  return 0;
}