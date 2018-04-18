/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_message_parser.hh"

using namespace std;

void WSMessageParser::parse(const string & buf)
{
  raw_buffer_.append(buf);

  /* repeatedly parse complete frames */
  while (not raw_buffer_.empty()) {
    uint64_t expected_length = WSFrame::expected_length(raw_buffer_);

    if (raw_buffer_.length() < expected_length) {
      /* still need more bytes to have a complete frame */
      return;
    }

    /* okay, we have a complete frame now! */
    WSFrame frame {raw_buffer_.substr(0, expected_length)};
    raw_buffer_.erase(0, expected_length);

    switch (frame.header().opcode()) {
    case WSFrame::OpCode::Continuation:
      if (frame_buffer_.size() == 0) {
        throw runtime_error("message cannot start with a continuation frame");
      }

      frame_buffer_.emplace_back(move(frame));
      break;

    case WSFrame::OpCode::Text:
    case WSFrame::OpCode::Binary:
      if (frame_buffer_.size() != 0) {
        throw runtime_error("expected a continuation message, got text/binary");
      }

      frame_buffer_.emplace_back(move(frame));
      break;

    case WSFrame::OpCode::Close:
    case WSFrame::OpCode::Ping:
    case WSFrame::OpCode::Pong:
      if (not frame.header().fin()) {
        throw runtime_error("control frames must not be fragmented");
      }

      /* we don't put control frames into the frame buffer, we directly create
      the message from those and push them into the output queue */
      complete_messages_.emplace(list<WSFrame>{frame});
      return;
    default:
      throw runtime_error("invalid opcode");
    }

    if (frame.header().fin()) {
      complete_messages_.emplace(frame_buffer_);
      frame_buffer_.clear();
    }
  }
}
