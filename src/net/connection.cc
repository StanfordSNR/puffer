/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "connection.hh"

using namespace std;

void Connection::write(const std::string & data)
{
  write_buffer_ += data;
}

std::string Connection::read()
{
  return socket_.read();
}
