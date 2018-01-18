/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "ws_frame.hh"

#include <endian.h>

using namespace std;

string put_field(const uint16_t n)
{
  const uint16_t network_order = htole16(n);
  return string(reinterpret_cast<const char *>(&network_order),
                sizeof(network_order));
}

string put_field(const uint32_t n)
{
  const uint32_t network_order = htole32(n);
  return string(reinterpret_cast<const char *>(&network_order),
                sizeof(network_order));
}

string put_field(const uint64_t n)
{
  const uint32_t network_order = htole64(n);
  return string(reinterpret_cast<const char *>(&network_order),
                sizeof(network_order));
}

WSFrame::WSFrame(const bool fin, const OpCode opcode, const string & payload)
  : fin_(fin), opcode_(opcode), payload_(payload)
{}

WSFrame::WSFrame(const bool fin, const OpCode opcode, const string & payload,
                 const uint32_t masking_key)
  : fin_(fin), opcode_(opcode), masking_key_(true, masking_key),
    payload_(payload)
{}

WSFrame::WSFrame(const Chunk & chunk)
  : fin_(chunk(0, 1).bits(7, 1)),
    opcode_(static_cast<WSFrame::OpCode>(chunk(4, 4).octet()))
{
  bool masked = chunk(1, 1).bits(7, 1);
  uint64_t payload_length = chunk(1, 1).bits(0, 7);
  size_t next_idx = 2;

  switch (payload_length) {
  case 126:
    payload_length = chunk(2, 2).le16();
    next_idx = 4;
    break;

  case 127:
    payload_length = chunk(2, 8).le64();
    next_idx = 10;
    break;

  default:
    break;
  }

  if (masked) {
    const string mk = chunk(next_idx, 4).to_string();
    masking_key_.reset(chunk(next_idx, 4).le32());
    next_idx += 4;

    const uint8_t * payload_buffer = chunk(next_idx).buffer();
    payload_.reserve(payload_length);

    for (size_t i = 0; i < payload_length; i++) {
      payload_.push_back(payload_buffer[i] ^ mk[i % 4]);
    }

    assert(payload_.length() == payload_length);
  }
  else {
    payload_ = chunk(next_idx).to_string();
  }
}

string WSFrame::to_string() const
{
  string output;
  uint8_t temp_byte;

  /* first byte */
  temp_byte = (fin_ << 8) + static_cast<uint8_t>(opcode_);
  output.push_back(temp_byte);

  /* second byte */
  temp_byte = (masking_key_.initialized() << 8);

  if (payload_.length() <= 125u) {
    temp_byte += static_cast<uint8_t>(payload_.length());
    output.push_back(temp_byte);
  }
  else if (payload_.length() < (1u << 16)) {
    temp_byte += static_cast<uint8_t>(126);
    output.push_back(temp_byte);
    output += put_field(static_cast<uint16_t>(payload_.length()));
  }
  else if (payload_.length() <= (1ull << 63) ){
    temp_byte += static_cast<uint8_t>(127);
    output.push_back(temp_byte);
    output += put_field(static_cast<uint64_t>(payload_.length()));
  }
  else {
    throw runtime_error("payload size > maximum allowed");
  }

  if (masking_key_.initialized()) {
    string mk = put_field(*masking_key_);
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
