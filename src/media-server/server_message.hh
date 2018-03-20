#ifndef SERVER_MESSAGE_HH
#define SERVER_MESSAGE_HH

#include <cstdint>
#include <string>
#include <vector>

#include "json.hpp"
using json = nlohmann::json;

class ServerMsg
{
public:
  enum class Type {
    Unknown,
    Hello,
    Init,
    Video,
    Audio
  };

  Type type() { return type_; }
  std::string type_str();

  /* serialize server message: "length(msg_)|serialized(msg_)" */
  std::string to_string();

protected:
  ServerMsg() {}

  /* mutators */
  void set_msg(json && msg) { msg_ = move(msg); }
  void set_type(const Type type) { type_ = type; }

private:
  json msg_ {};
  Type type_ {Type::Unknown};
};

class ServerHelloMsg : public ServerMsg
{
public:
  ServerHelloMsg(const std::vector<std::string> & channels);
};

class ServerInitMsg : public ServerMsg
{
public:
  ServerInitMsg(const std::string & channel,
                const std::string & video_codec,
                const std::string & audio_codec,
                const unsigned int timescale,
                const uint64_t init_vts,
                const uint64_t init_ats,
                const unsigned int init_id,
                const bool can_resume);
};

class ServerVideoMsg : public ServerMsg
{
public:
  ServerVideoMsg(const std::string & quality,
                 const unsigned int timestamp,
                 const unsigned int duration,
                 const unsigned int byte_offset,
                 const unsigned int total_byte_length);
};

class ServerAudioMsg : public ServerMsg
{
public:
  ServerAudioMsg(const std::string & quality,
                 const unsigned int timestamp,
                 const unsigned int duration,
                 const unsigned int byte_offset,
                 const unsigned int total_byte_length);
};

#endif /* SERVER_MESSAGE_HH */
