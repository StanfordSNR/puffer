#include "client_message.hh"

#include <cassert>

using namespace std;

ClientMsgParser::ClientMsgParser(const string & data)
{
  msg_ = json::parse(data);

  auto it = msg_.find("type");
  if (it == msg_.end()) {
    throw runtime_error("Cannot find message type");
  }

  string type_str = *it;

  if (type_str == "client-init") {
    type_ = Type::Init;
  } else if (type_str == "client-info") {
    type_ = Type::Info;
  } else {
    throw runtime_error("Invalid message type");
  }
}

ClientInitMsg ClientMsgParser::parse_init_msg()
{
  assert(type_ == Type::Init);

  ClientInitMsg ret;

  ret.user_id = msg_.at("userId");

  auto it = msg_.find("channel");
  if (it != msg_.end()) {
    ret.channel = *it;
  }

  ret.player_width = msg_.at("playerWidth");
  ret.player_height = msg_.at("playerHeight");

  it = msg_.find("nextVideoTimestamp");
  if (it != msg_.end()) {
    ret.next_vts = *it;
  }

  it = msg_.find("nextAudioTimestamp");
  if (it != msg_.end()) {
    ret.next_ats = *it;
  }

  return ret;
}

ClientInfoMsg ClientMsgParser::parse_info_msg()
{
  assert(type_ == Type::Info);

  ClientInfoMsg ret;

  string event_str = msg_.at("event");

  if (event_str == "timer") {
    ret.event = ClientInfoMsg::PlayerEvent::Timer;
  } else if (event_str == "rebuffer") {
    ret.event = ClientInfoMsg::PlayerEvent::Rebuffer;
  } else if (event_str == "canplay") {
    ret.event = ClientInfoMsg::PlayerEvent::CanPlay;
  } else if (event_str == "audack") {
    ret.event = ClientInfoMsg::PlayerEvent::AudioAck;
  } else if (event_str == "vidack") {
    ret.event = ClientInfoMsg::PlayerEvent::VideoAck;
  } else {
    ret.event = ClientInfoMsg::PlayerEvent::Unknown;
  }

  ret.init_id = msg_.at("initId");
  ret.video_buffer_len = msg_.at("videoBufferLen");
  ret.audio_buffer_len = msg_.at("audioBufferLen");
  ret.next_video_timestamp = msg_.at("nextVideoTimestamp");
  ret.next_audio_timestamp = msg_.at("nextAudioTimestamp");
  ret.player_width = msg_.at("playerWidth");
  ret.player_height = msg_.at("playerHeight");

  int player_ready_state = msg_.at("playerReadyState");
  if (player_ready_state < 0 || player_ready_state > 4) {
    throw runtime_error("Invalid player ready state");
  }
  ret.player_ready_state = static_cast<ClientInfoMsg::PlayerReadyState>
                                      (player_ready_state);

  return ret;
}
