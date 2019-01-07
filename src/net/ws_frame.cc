/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_frame.hh"
#include "serialization.hh"

#include <iostream>
#include <endian.h>

using namespace std;

WSFrame::Header::Header(const Chunk & chunk)
{
  if (chunk.size() < 2) {
    throw out_of_range("incomplete header");
  }

  fin_ = chunk(0 , 1).bits(7, 1);
  opcode_ = static_cast<WSFrame::OpCode>(chunk(0, 1).bits(0, 4));

  bool masked = chunk(1, 1).bits(7, 1);
  payload_length_ = chunk(1, 1).bits(0, 7);
  size_t next_idx = 2;

  switch (payload_length_) {
  case 126:
    if (chunk.size() < 4) {
      throw out_of_range("incomplete header");
    }

    payload_length_ = chunk(2, 2).be16();
    next_idx = 4;
    break;

  case 127:
    if (chunk.size() < 10) {
      throw out_of_range("incomplete header");
    }

    payload_length_ = chunk(2, 8).be64();
    next_idx = 10;
    break;

  default:
    break;
  }

  if (masked) {
    if (chunk.size() < (next_idx + 4)) {
      throw out_of_range("incomplete header: missing masking key");
    }

    masking_key_ = chunk(next_idx, 4).be32();
  }
}

WSFrame::Header::Header(const bool fin, const OpCode opcode,
                        const uint64_t payload_length)
  : fin_(fin), opcode_(opcode), payload_length_(payload_length)
{}

WSFrame::Header::Header(const bool fin, const OpCode opcode,
                        const uint64_t payload_length,
                        const uint32_t masking_key)
  : fin_(fin), opcode_(opcode), payload_length_(payload_length),
    masking_key_(masking_key)
{}

uint64_t WSFrame::expected_length( const Chunk & chunk )
{
  if ( chunk.size() < 2 ) {
    /* this is the minimum size of a frame */
    return 2;
  }

  uint64_t payload_length = chunk(1, 1).bits(0, 7);
  bool masked = chunk(1, 1).bits(7, 1);

  switch (payload_length) {
  case 126:
    if (chunk.size() < 4) {
      /* we need at least 4 bytes to determine the size of this frame */
      return 4;
    }

    return 4 + chunk(2, 2).be16() + (masked ? 4 : 0);

  case 127:
    if (chunk.size() < 10) {
      /* we need at least 10 bytes to determine the size of this frame */
      return 10;
    }

    return 10 + chunk(2, 8).be64() + (masked ? 4 : 0);

  default:
    return 2 + payload_length + (masked ? 4 : 0);
  }
}

uint32_t WSFrame::Header::header_length() const
{
  return 2 + ((payload_length_ < 126) ?
               0 : ((payload_length_ < (1 << 16)) ? 2 : 8))
           + (masking_key_ ? 4 : 0);
}

WSFrame::WSFrame(const bool fin, const OpCode opcode, const string & payload)
  : header_(fin, opcode, payload.length()), payload_(payload)
{}

WSFrame::WSFrame(const bool fin, const OpCode opcode, const string & payload,
                 const uint32_t masking_key)
  : header_(fin, opcode, payload.length(), masking_key), payload_(payload)
{}

WSFrame::WSFrame(const bool fin, const OpCode opcode, string && payload)
  : header_(fin, opcode, payload.length()), payload_(move(payload))
{}

WSFrame::WSFrame(const bool fin, const OpCode opcode, string && payload,
                 const uint32_t masking_key)
  : header_(fin, opcode, payload.length(), masking_key),
    payload_(move(payload))
{}

WSFrame::WSFrame(const Chunk & chunk)
  : header_(chunk),
    payload_(chunk(header_.header_length()).to_string())
{
  if (payload_.length() != header_.payload_length()) {
    throw out_of_range("payload size doesn't match the size in the header");
  }

  if (header_.masking_key()) {
    const string mk = put_field(*header_.masking_key());

    for (size_t i = 0; i < payload_.length(); i++) {
      payload_[i] ^= mk[i % 4];
    }
  }
}

string WSFrame::to_string() const
{
  string output;
  uint8_t temp_byte;

  /* first byte */
  temp_byte = (header_.fin() << 7) + static_cast<uint8_t>(header_.opcode());
  output.push_back(temp_byte);

  /* second byte */
  temp_byte = header_.masking_key() ? 1 << 7 : 0;

  if (payload_.length() <= 125u) {
    temp_byte += static_cast<uint8_t>(payload_.length());
    output.push_back(temp_byte);
  }
  else if (payload_.length() < (1u << 16)) {
    temp_byte += static_cast<uint8_t>(126);
    output.push_back(temp_byte);
    output += put_field(static_cast<uint16_t>(payload_.length()));
  }
  else if (payload_.length() <= (1ull << 63)){
    temp_byte += static_cast<uint8_t>(127);
    output.push_back(temp_byte);
    output += put_field(static_cast<uint64_t>(payload_.length()));
  }
  else {
    throw runtime_error("payload size > maximum allowed");
  }

  if (header_.masking_key()) {
    string mk = put_field(*header_.masking_key());
    output += mk;

    string masked_payload;
    masked_payload.reserve(payload_.length());

    for (size_t i = 0; i < payload_.length(); i++) {
      masked_payload.push_back(payload_[i] ^ mk[i % 4]);
    }
  }
  else {
    output += payload_;
  }

  return output;
}
