#include "ipc_socket.hh"
#include "poller.hh"

class PortusClient
{
public:
  PortusClient(Poller & poller,
               const std::string & path);

private:
  IPCSocket sock_ {};

  std::string payload_ {};
};
