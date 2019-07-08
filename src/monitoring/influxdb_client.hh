#include <string>
#include <deque>

#include "socket.hh"
#include "poller.hh"

class InfluxDBClient
{
public:
  InfluxDBClient(Poller & poller,
                 const Address & address,
                 const std::string & database,
                 const std::string & user,
                 const std::string & password);

  void post(const std::string & payload,
            const std::string & precision = "ms");

private:
  Address influxdb_addr_ {};
  TCPSocket sock_ {};

  std::string database_ {};
  std::string user_ {};
  std::string password_ {};

  std::deque<std::string> buffer_ {};
  size_t buffer_offset_ {0};
};
