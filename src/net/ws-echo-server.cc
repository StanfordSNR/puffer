/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <iostream>

#include "ws_server.hh"

using namespace std;

int main()
{
  string ip = "0.0.0.0";
  uint16_t port = 9333;

  WSServer ws_server {{ip,port}};
  ws_server.serve_forever();

  return 0;
}
