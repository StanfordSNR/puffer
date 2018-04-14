/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WS_MESSAGE_PARSER_HH
#define WS_MESSAGE_PARSER_HH

#include <string>
#include <queue>
#include <list>

#include "ws_message.hh"
#include "ws_frame.hh"

class WSMessageParser
{
private:
  std::string raw_buffer_ {};
  std::list<WSFrame> frame_buffer_ {};
  std::queue<WSMessage> complete_messages_ {};

public:
  void parse(const std::string & buf);

  bool empty() const { return complete_messages_.empty(); }

  const WSMessage & front() const { return complete_messages_.front(); }
  WSMessage & front() { return complete_messages_.front(); }

  void pop() { complete_messages_.pop(); }
};

#endif /* WS_MESSAGE_PARSER_HH */
