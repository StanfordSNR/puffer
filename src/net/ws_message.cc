/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_message.hh"

#include <cassert>
#include <initializer_list>

using namespace std;

WSMessage::WSMessage(const WSFrame & frame)
  : WSMessage(list<WSFrame>{frame})
{}

WSMessage::WSMessage(const list<WSFrame> & frames)
{
  if (frames.size() == 0) {
    throw runtime_error( "no frames to create a message" );
  }

  size_t i = 0;
  for (const WSFrame & frame : frames) {
    if (i == 0 and frame.header().opcode() == WSFrame::OpCode::Continuation) {
      throw runtime_error("first frame cannot be a continuation frame");
    }

    if ((i == frames.size() - 1) and (not frame.header().fin())) {
      throw runtime_error("last frame doesn't have fin flag set");
    }

    if (i != 0 and frame.header().opcode() != WSFrame::OpCode::Continuation) {
      throw runtime_error("invalid opcode");
    }

    if (i == 0) {
      switch (frame.header().opcode()) {
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

        default:
          assert(false);  /* will not happen */
          break;
      }
    }

    payload_ += frame.payload();
    i++;
  }
}
