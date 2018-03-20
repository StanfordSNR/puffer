#include "server_message.hh"

using namespace std;

string ServerMsg::type_str()
{
  switch (type_) {
    case Type::Unknown:
      throw runtime_error("Cannot convert an unknown type to a string");
    case Type::Hello:
      return "server-hello";
    case Type::Init:
      return "server-init";
    case Type::Video:
      return "server-video";
    case Type::Audio:
      return "server-audio";
    default:
      throw runtime_error("Invalid type");
  }
}

string ServerMsg::to_string()
{
  string msg_str = msg_.dump();
  uint16_t msg_len = msg_str.length();
  string ret(sizeof(uint16_t) + msg_len, 0);

  /* Network endian */
  uint16_t msg_len_be = htobe16(msg_len);
  memcpy(&ret[0], &msg_len_be, sizeof(uint16_t));

  copy(msg_str.begin(), msg_str.end(), ret.begin() + sizeof(uint16_t));
  return ret;
}

ServerHelloMsg::ServerHelloMsg(const vector<string> & channels)
{
  set_type(Type::Hello);
  set_msg({
    {"type", type_str()},
    {"channels", json(channels)}
  });
}

ServerInitMsg::ServerInitMsg(const string & channel,
                             const string & video_codec,
                             const string & audio_codec,
                             const unsigned int timescale,
                             const uint64_t init_vts,
                             const uint64_t init_ats,
                             const unsigned int init_id,
                             const bool can_resume)
{
  set_type(Type::Init);
  set_msg({
    {"type", type_str()},
    {"channel", channel},
    {"videoCodec", video_codec},
    {"audioCodec", audio_codec},
    {"timescale", timescale},
    {"initVideoTimestamp", init_vts},
    {"initAudioTimestamp", init_ats},
    {"initId", init_id},
    {"canResume", can_resume},
  });
}

ServerVideoMsg::ServerVideoMsg(const string & quality,
                               const unsigned int timestamp,
                               const unsigned int duration,
                               const unsigned int byte_offset,
                               const unsigned int total_byte_length)
{
  set_type(Type::Video);
  set_msg({
    {"type", type_str()},
    {"quality", quality},
    {"timestamp", timestamp},
    {"duration", duration},
    {"byteOffset", byte_offset},
    {"totalByteLength", total_byte_length}
  });
}

ServerAudioMsg::ServerAudioMsg(const string & quality,
                               const unsigned int timestamp,
                               const unsigned int duration,
                               const unsigned int byte_offset,
                               const unsigned int total_byte_length)
{
  set_type(Type::Audio);
  set_msg({
    {"type", type_str()},
    {"quality", quality},
    {"timestamp", timestamp},
    {"duration", duration},
    {"byteOffset", byte_offset},
    {"totalByteLength", total_byte_length}
  });
}
