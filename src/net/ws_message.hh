/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WS_MESSAGE_HH
#define WS_MESSAGE_HH

#include <string>
#include <vector>

#include "ws_frame.hh"

class WSMessage
{
public:
  enum class Type
  {
    Text, Binary, Close, Ping, Pong
  };

private:
  Type type_;
  std::string payload_;

public:
  WSMessage(const Type type, std::string payload);

  Type type() const { return type_; }
  const std::string & payload() const { return payload_; }
};

#endif /* WS_MESSAGE_HH */
