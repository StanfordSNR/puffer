/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <iostream>
#include <string>

#include "poller.hh"
#include "address.hh"
#include "socket.hh"
#include "secure_socket.hh"
#include "nb_secure_socket.hh"

using namespace std;
using namespace PollerShortNames;

int main()
{
  Address google_addr {"www.google.com", "443"};
  TCPSocket sock;
  SSLContext ssl_context;

  sock.set_blocking(false);
  try {
    sock.connect(google_addr);
    throw runtime_error("nonblocking connect unexpectedly succeeded immediately");
  }
  catch (const unix_error & e) {
    if (e.error_code() == EINPROGRESS) {
      /* do nothing */
    } else {
      throw;
    }
  }
  NBSecureSocket secure_socket {ssl_context.new_secure_socket(move(sock))};

  const string http_request =
    "GET / HTTP/1.1\r\n"
    "Host: www.google.com\r\n"
    "User-Agent: curl/7.55.1\r\n"
    "Accept: */*\r\n"
    "\r\n";

  bool request_sent = false;

  Poller poller;

  poller.add_action(Poller::Action (secure_socket, Direction::Out,
    [&http_request, &request_sent, &secure_socket]()
    {
      secure_socket.ezwrite(http_request);
      request_sent = true;
      return ResultType::Continue;
    },
    [&request_sent]() { return not request_sent; }
  ));

  poller.add_action(Poller::Action (secure_socket, Direction::In,
    [&secure_socket]()
    {
      cout << secure_socket.ezread();
      return ResultType::Continue;
    }
  ));

  while ( poller.poll( -1 ).result == Poller::Result::Type::Success );

  return EXIT_SUCCESS;
}
