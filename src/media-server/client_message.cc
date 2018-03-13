#include "client_message.h"

#include <algorithm>
#include <iostream>
#include <endian.h>

#include "json.hpp"

using namespace std;
using json = nlohmann::json;

pair<ClientMsg::Type, string> unpack_client_msg(const string & data) {
  int split_idx = data.find_first_of(' ');
  if (split_idx == string::npos) {
    throw ParseExeception("Invalid message from the client");
  }
  string type_str = data.substr(0, split_idx);
  ClientMsg::Type type;
  if (type_str == "client-init") {
    type = ClientMsg::Init;
  } else if (type_str == "client-info") {
    type = ClientMsg::Info;
  } else {
    type = ClientMsg::Unknown;
  }
  return make_pair(type, data.substr(split_idx));
}

ClientInit parse_client_init_msg(const string & data) 
{
  ClientInit ret;
  try {
    auto obj = json::parse(data);
    ret.channel = obj["channel"];
    ret.player_width = obj["playerWidth"];
    ret.player_height = obj["playerHeight"];
  } catch (const exception & e) {
    throw ParseExeception(e.what());
  }
  return ret;
}

ClientInfo parse_client_info_msg(const string & data) 
{
  ClientInfo ret;
  try {
    auto obj = json::parse(data);
    string event_str = obj["event"];

    ClientInfo::PlayerEvent event;
    if (event_str == "timer") {
      event = ClientInfo::Timer;
    } else if (event_str == "rebuffer") {
      event = ClientInfo::Rebuffer;
    } else if (event_str == "canplay") {
      event = ClientInfo::CanPlay;
    } else {
      event = ClientInfo::Unknown;
    }

    ret.event = event;
    ret.video_buffer_len = obj["videoBufferLen"];
    ret.audio_buffer_len = obj["audioBufferLen"];
    ret.next_video_timestamp = obj["nextVideoTimestamp"];
    ret.next_audio_timestamp = obj["nextAudioTimestamp"];
    ret.player_width = obj["playerWidth"];
    ret.player_height = obj["playerHeight"];
    
    int player_ready_state = obj["playerReadyState"];
    if (player_ready_state < 0 || player_ready_state > 4) {
      throw ParseExeception("Invalid player ready state");
    }
    ret.player_ready_state = static_cast<ClientInfo::PlayerReadyState>(player_ready_state);

  } catch (const ParseExeception & e) {
    throw e;
  } catch (const exception & e) {
    throw ParseExeception(e.what());
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