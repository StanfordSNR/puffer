#include "portus_client.hh"

#include <iostream>

using namespace std;
using namespace PollerShortNames;

PortusClient::PortusClient(Poller & poller,
                           const string & path)
{
  sock_.bind(path);
  sock_.listen();

  poller.add_action(Poller::Action(sock_, Direction::In,
    [this]()->Result {
      /* TODO: add callbacks when receiving message from portus*/
      return ResultType::Continue;
    }
  ));

  poller.add_action(Poller::Action(sock_, Direction::Out,
    [this]()->Result {
      /* send message to portus */
      sock_.write(payload_);
      payload_.clear();

      return ResultType::Continue;
    },
    [this]()->bool {
      return not payload_.empty();
    }
  ));
}
