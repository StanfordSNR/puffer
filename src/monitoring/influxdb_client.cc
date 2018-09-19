#include "influxdb_client.hh"

#include <iostream>
#include "http_request.hh"

using namespace std;
using namespace PollerShortNames;

InfluxDBClient::InfluxDBClient(Poller & poller,
                               const Address & address,
                               const string & database,
                               const string & user,
                               const string & password)
{
  influxdb_addr_ = address;
  sock_.connect(influxdb_addr_);
  http_request_line_ = "POST /write?db=" + database + "&u=" + user + "&p="
                       + password + "&precision=s HTTP/1.1";

  poller.add_action(Poller::Action(sock_, Direction::In,
    [this]()->Result {
      /* must read HTTP responses from InfluxDB, then basically ignore them */
      const string response = sock_.read();
      if (response.empty()) {
        throw runtime_error("peer socket in InfluxDB has closed");
      }

      return ResultType::Continue;
    }
  ));

  poller.add_action(Poller::Action(sock_, Direction::Out,
    [this]()->Result {
      /* send POST request to InfluxDB */
      HTTPRequest request;
      request.set_first_line(http_request_line_);
      request.add_header(HTTPHeader{"Host", influxdb_addr_.str()});
      request.add_header(HTTPHeader{"Content-Type",
                                    "application/x-www-form-urlencoded"});
      request.add_header(HTTPHeader{"Content-Length",
                                    to_string(payload_.size())});
      request.done_with_headers();
      request.read_in_body(payload_);

      sock_.write(request.str());
      payload_.clear();

      return ResultType::Continue;
    },
    [this]()->bool {
      return not payload_.empty();
    }
  ));
}

void InfluxDBClient::post(const string & payload)
{
  payload_ += payload + "\n";
}
