/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WS_MESSAGE_HH
#define WS_MESSAGE_HH

#include <string>
#include <vector>
#include <list>

#include "ws_frame.hh"

class WSMessage
{
public:
  enum class Type
  {
    Text, Binary, Close, Ping, Pong
  };

private:
  Type type_ {Type::Text};
  std::string payload_ {};

public:
  WSMessage(const WSFrame & frame);
  WSMessage(const std::list<WSFrame> & frames);
  WSMessage(const Type type, const std::string & payload);

  Type type() const { return type_; }
  const std::string & payload() const { return payload_; }
};

#endif /* WS_MESSAGE_HH */
