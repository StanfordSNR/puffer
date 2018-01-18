/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WS_FRAME_HH
#define WS_FRAME_HH

#include <string>
#include "optional.hh"

class WebSocketFrame
{
public:
  enum class OpCode : uint8_t
  {
    Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8,
    Ping = 0x9, Pong = 0xA
  };

private:
  bool fin_ {false};
  OpCode opcode_ {OpCode::Binary};
  Optional<uint32_t> masking_key_ {false};
  std::string payload_ {};

public:
  void set_fin(const bool fin) { fin_ = fin; }
  void set_opcode(const OpCode opcode) { opcode_ = opcode; }
  void set_masking(const uint32_t masking_key) { masking_key_.reset(masking_key); }
  void set_payload(const std::string & payload) { payload_ = payload; }

  bool fin() const { return fin_; }
  OpCode opcode() const { return opcode_; }
  uint64_t payload_length() const { return payload_.length(); }
  bool masked() const { return masking_key_.initialized(); }
  const std::string & payload() const { return payload_; }

  /* serialize a frame */
  std::string to_string() const;
};

#endif /* WS_FRAME_HH */
