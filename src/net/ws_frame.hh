/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WS_FRAME_HH
#define WS_FRAME_HH

class WebSocketFrame
{
public:
  enum class OpCode
  {
    Continuation,
    Text,
    Binary,
    Close,
    Ping,
    Pong
  };

private:
  bool fin_;
  OpCode opcode_;
  uint64_t payload_length_;
  Optional<uint32_t> masking_key_;
  string payload_;

public:
  bool fin() const { return fin_; }
  OpCode opcode() const { return opcode_; }
  uint64_t payload_length() const { return payload_length_; }
  bool masked() const { return masking_key_.initialized(); }
  const string & payload() const { return payload_; }
};

#endif /* WS_FRAME_HH */
