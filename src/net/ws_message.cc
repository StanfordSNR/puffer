/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_message.hh"

using namespace std;

WSMessage::WSMessage(const WSFrame & frame)
{
  if (not frame.header().fin() or
      frame.header().opcode() == WSFrame::OpCode::Continuation) {
    throw runtime_error("cannot turn frame into message");
  }

  switch (frame.header().opcode()) {
    case WSFrame::OpCode::Continuation:
      break; /* not gonna happen */

    case WSFrame::OpCode::Text:
      type_ = WSMessage::Type::Text;
      break;

    case WSFrame::OpCode::Binary:
      type_ = WSMessage::Type::Binary;
      break;

    case WSFrame::OpCode::Close:
      type_ = WSMessage::Type::Close;
      break;

    case WSFrame::OpCode::Ping:
      type_ = WSMessage::Type::Ping;
      break;

    case WSFrame::OpCode::Pong:
      type_ = WSMessage::Type::Pong;
      break;
  }

  payload_ = frame.payload();
}
