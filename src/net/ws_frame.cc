/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_frame.hh"

void WebSocketFrame::set_payload(const std::string & payload)
{
  payload_ = payload;
  payload_length_ = payload.length();
}
