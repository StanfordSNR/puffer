/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#ifndef WS_FRAME_HH
#define WS_FRAME_HH

#include <string>
#include <optional>

#include "chunk.hh"

class WSFrame
{
public:
  enum class OpCode : uint8_t
  {
    Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8,
    Ping = 0x9, Pong = 0xA
  };

  class Header
  {
  private:
    bool fin_ {false};
    OpCode opcode_ {OpCode::Text};
    uint64_t payload_length_ {0};
    std::optional<uint32_t> masking_key_ {};

  public:
    Header(const Chunk & chunk);
    Header(const bool fin, const OpCode opcode, const uint64_t payload_length);
    Header(const bool fin, const OpCode opcode, const uint64_t payload_length,
           const uint32_t masking_key);

    bool fin() const { return fin_; }
    OpCode opcode() const { return opcode_; }
    uint64_t payload_length() const { return payload_length_; }
    std::optional<uint32_t> masking_key() const { return masking_key_; }

    uint32_t header_length() const;
  };

private:
  Header header_;
  std::string payload_ {};

public:
  WSFrame(const Chunk & chunk);
  WSFrame(const bool fin, const OpCode opcode, const std::string & payload);
  WSFrame(const bool fin, const OpCode opcode, const std::string & payload,
          const uint32_t masking_key);
  WSFrame(const bool fin, const OpCode opcode, std::string && payload);
  WSFrame(const bool fin, const OpCode opcode, std::string && payload,
          const uint32_t masking_key);

  const Header & header() const { return header_; }
  const std::string & payload() const { return payload_; }

  /* serialize a frame */
  std::string to_string() const;

  static uint64_t expected_length( const Chunk & chunk );
};

#endif /* WS_FRAME_HH */
