#include "client_message.hh"

using namespace std;

ClientInitMsg::ClientInitMsg(const json & msg)
{
  init_id = msg.at("initId").get<unsigned int>();
  channel = msg.at("channel").get<string>();

  session_key = msg.at("sessionKey").get<string>();
  username = msg.at("userName").get<string>();

  os = msg.at("os").get<string>();
  browser = msg.at("browser").get<string>();

  screen_width = msg.at("screenWidth").get<uint16_t>();
  screen_height = msg.at("screenHeight").get<uint16_t>();

  auto it = msg.find("nextVts");
  if (it != msg.end()) {
    next_vts = it->get<uint64_t>();
  }

  it = msg.find("nextAts");
  if (it != msg.end()) {
    next_ats = it->get<uint64_t>();
  }
}

ClientInfoMsg::ClientInfoMsg(const json & msg)
{
  init_id = msg.at("initId").get<unsigned int>();

  event_str = msg.at("event").get<string>();
  if (event_str == "timer") {
    event = ClientInfoMsg::Event::Timer;
  } else if (event_str == "startup") {
    event = ClientInfoMsg::Event::Startup;
  } else if (event_str == "rebuffer") {
    event = ClientInfoMsg::Event::Rebuffer;
  } else if (event_str == "play") {
    event = ClientInfoMsg::Event::Play;
  } else {
    throw runtime_error("Invalid client info event");
  }

  video_buffer = msg.at("videoBuffer").get<double>();
  audio_buffer = msg.at("audioBuffer").get<double>();
  cum_rebuffer = msg.at("cumRebuffer").get<double>();

  auto it = msg.find("screenWidth");
  if (it != msg.end()) {
    screen_width = it->get<uint16_t>();
  }

  it = msg.find("screenHeight");
  if (it != msg.end()) {
    screen_height = it->get<uint16_t>();
  }
}

ClientAckMsg::ClientAckMsg(const json & msg)
{
  init_id = msg.at("initId").get<unsigned int>();

  channel = msg.at("channel").get<string>();
  format = msg.at("format").get<string>();
  timestamp = msg.at("timestamp").get<uint64_t>();

  byte_offset = msg.at("byteOffset").get<unsigned int>();
  byte_length = msg.at("byteLength").get<unsigned int>();
  total_byte_length = msg.at("totalByteLength").get<unsigned int>();

  video_buffer = msg.at("videoBuffer").get<double>();
  audio_buffer = msg.at("audioBuffer").get<double>();
  cum_rebuffer = msg.at("cumRebuffer").get<double>();
}

ClientVidAckMsg::ClientVidAckMsg(const json & msg)
  : ClientAckMsg(msg), video_format(format)
{
  ssim = msg.at("ssim").get<double>();
}

ClientAudAckMsg::ClientAudAckMsg(const json & msg)
  : ClientAckMsg(msg), audio_format(format)
{}

ClientMsgParser::ClientMsgParser(const string & data)
  : msg_(json::parse(data))
{
  const string & type_str = msg_.at("type").get<string>();

  if (type_str == "client-init") {
    type_ = Type::Init;
  } else if (type_str == "client-info") {
    type_ = Type::Info;
  } else if (type_str == "client-vidack") {
    type_ = Type::VideoAck;
  } else if (type_str == "client-audack") {
    type_ = Type::AudioAck;
  } else {
    throw runtime_error("Invalid client message type");
  }
}

ClientInitMsg ClientMsgParser::parse_client_init()
{
  return ClientInitMsg(msg_);
}

ClientInfoMsg ClientMsgParser::parse_client_info()
{
  return ClientInfoMsg(msg_);
}

ClientVidAckMsg ClientMsgParser::parse_client_vidack()
{
  return ClientVidAckMsg(msg_);
}

ClientAudAckMsg ClientMsgParser::parse_client_audack()
{
  return ClientAudAckMsg(msg_);
}
