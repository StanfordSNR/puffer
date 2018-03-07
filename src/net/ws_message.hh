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
  using Type = WSFrame::OpCode;

private:
  Type type_ {Type::Text};
  std::string payload_ {};

public:
  WSMessage(const WSFrame & frame);
  WSMessage(const std::list<WSFrame> & frames);

  Type type() const { return type_; }
  const std::string & payload() const { return payload_; }
};

#endif /* WS_MESSAGE_HH */
