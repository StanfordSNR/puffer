#include "message.hh"

#include <algorithm>
#include <iostream>
#include <endian.h>

#include "json.hpp"

using namespace std;
using json = nlohmann::json;

pair<ClientMessageType, string> unpack_client_msg(const string & data) {
  size_t split_idx = data.find_first_of(' ');
  if (split_idx == string::npos) {
    throw BadClientMessageException("Cannot get message type");
  }
  string type_str = data.substr(0, split_idx);
  ClientMessageType type;
  if (type_str == "client-init") {
    type = ClientMessageType::Init;
  } else if (type_str == "client-info") {
    type = ClientMessageType::Info;
  } else {
    type = ClientMessageType::Unknown;
  }
  return make_pair(type, data.substr(split_idx));
}

ClientInitMessage parse_client_init_msg(const string & data)
{
  optional<string> channel;
  int player_width, player_height;
  try {
    auto obj = json::parse(data);
    auto it = obj.find("channel");
    if (it != obj.end()) {
      channel = *it;
    }
    player_width = obj.at("playerWidth");
    player_height = obj.at("playerHeight");
  } catch (const exception & e) {
    throw BadClientMessageException(e.what());
  }
  return {channel, player_width, player_height};
}

ClientInfoMessage parse_client_info_msg(const string & data)
{
  ClientInfoMessage ret;
  try {
    auto obj = json::parse(data);
    string event_str = obj.at("event");

    ClientInfoMessage::PlayerEvent event;
    if (event_str == "timer") {
      event = ClientInfoMessage::PlayerEvent::Timer;
    } else if (event_str == "rebuffer") {
      event = ClientInfoMessage::PlayerEvent::Rebuffer;
    } else if (event_str == "canplay") {
      event = ClientInfoMessage::PlayerEvent::CanPlay;
    } else if (event_str == "audack") {
      event = ClientInfoMessage::PlayerEvent::AudioAck;
    } else if (event_str == "vidack") {
      event = ClientInfoMessage::PlayerEvent::VideoAck;
    } else {
      event = ClientInfoMessage::PlayerEvent::Unknown;
    }

    ret.event = event;
    ret.video_buffer_len = obj.at("videoBufferLen");
    ret.audio_buffer_len = obj.at("audioBufferLen");
    ret.next_video_timestamp = obj.at("nextVideoTimestamp");
    ret.next_audio_timestamp = obj.at("nextAudioTimestamp");
    ret.player_width = obj.at("playerWidth");
    ret.player_height = obj.at("playerHeight");

    int player_ready_state = obj.at("playerReadyState");
    if (player_ready_state < 0 || player_ready_state > 4) {
      throw BadClientMessageException("Invalid player ready state");
    }
    ret.player_ready_state = static_cast<ClientInfoMessage::PlayerReadyState>(player_ready_state);

  } catch (const BadClientMessageException & e) {
    throw e;
  } catch (const exception & e) {
    throw BadClientMessageException(e.what());
  }
  return ret;
}

static inline string pack_json(const json & msg)
{
  string msg_str = msg.dump();
  uint16_t msg_len = msg_str.length();
  string ret(sizeof(uint16_t) + msg_len, 0);

  /* Network endian */
  uint16_t msg_len_no = htobe16(msg_len);
  memcpy(&ret[0], &msg_len_no, sizeof(uint16_t));

  copy(msg_str.begin(), msg_str.end(), ret.begin() + sizeof(uint16_t));
  return ret;
}

string make_server_hello_msg(const vector<string> & channels)
{
  json msg = {
    {"type", "server-hello"},
    {"channels", json(channels)}
  };
  return pack_json(msg);
}

string make_server_init_msg(const string & channel,
                                  const string & video_codec,
                                  const string & audio_codec,
                                  const unsigned int & timescale,
                                  const unsigned int & init_timestamp)
{
  json msg = {
    {"type", "server-init"},
    {"channel", channel},
    {"videoCodec", video_codec},
    {"audioCodec", audio_codec},
    {"timescale", timescale},
    {"initTimestamp", init_timestamp}
  };
  return pack_json(msg);
}

static inline string make_media_chunk_msg(
  const string & media_type,
  const string & quality,
  const unsigned int & timestamp,
  const unsigned int & duration,
  const unsigned int & byte_offset,
  const unsigned int & total_byte_length)
{
  json msg = {
    {"type", media_type},
    {"quality", quality},
    {"timestamp", timestamp},
    {"duration", duration},
    {"byteOffset", byte_offset},
    {"totalByteLength", total_byte_length}
  };
  return pack_json(msg);
}

string make_audio_msg(
  const string & quality,
  const unsigned int & timestamp,
  const unsigned int & duration,
  const unsigned int & byte_offset,
  const unsigned int & total_byte_length)
{
  return make_media_chunk_msg("audio", quality, timestamp, duration,
                              byte_offset, total_byte_length);
}

string make_video_msg(
  const string & quality,
  const unsigned int & timestamp,
  const unsigned int & duration,
  const unsigned int & byte_offset,
  const unsigned int & total_byte_length)
{
  return make_media_chunk_msg("video", quality, timestamp, duration,
                              byte_offset, total_byte_length);
}
