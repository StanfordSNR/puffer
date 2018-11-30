#ifndef CLIENT_MESSAGE_HH
#define CLIENT_MESSAGE_HH

#include <cstdint>
#include <string>
#include <optional>
#include <exception>
#include <memory>

#include "media_formats.hh"
#include "json.hpp"

using json = nlohmann::json;

class ClientMsg
{
protected:
  /* prevent this class from being instantiated */
  ClientMsg() {}
};

class ClientInitMsg : public ClientMsg
{
public:
  ClientInitMsg(const json & msg);

  unsigned int init_id {};

  /* channel requested by the client to play
   * init_id and channel have a one-to-one mapping */
  std::string channel {};

  /* authentication */
  std::string session_key {};
  std::string username {};

  /* client OS, browser and screen information */
  std::string os {};
  std::string browser {};

  uint16_t screen_width {};
  uint16_t screen_height {};

  /* next timestamps to expect; used to resume connection only */
  std::optional<uint64_t> next_vts {};
  std::optional<uint64_t> next_ats {};
};

class ClientInfoMsg : public ClientMsg
{
public:
  enum class Event {
    Timer,    // 4Hz timer
    Startup,  // channel starts playing
    Rebuffer, // channel rebuffers
    Play      // channel resumes playing
  };

  ClientInfoMsg(const json & msg);

  unsigned int init_id {};

  Event event {};
  std::string event_str {};
  double video_buffer {};
  double audio_buffer {};
  double cum_rebuffer {};

  /* user's screen size might have changed while watching */
  std::optional<uint16_t> screen_width {};
  std::optional<uint16_t> screen_height {};
};

class ClientAckMsg : public ClientMsg
{
public:
  unsigned int init_id {};

  std::string channel {};  /* acked channel */
  std::string format {};   /* acked format */
  uint64_t timestamp {};   /* acked timestamp */

  unsigned int byte_offset {};  /* acked offset */
  unsigned int byte_length {};  /* acked length */
  unsigned int total_byte_length {};  /* total length to expect */

  double video_buffer {};
  double audio_buffer {};
  double cum_rebuffer {};

protected:
  /* prevent this class from being instantiated */
  ClientAckMsg(const json & msg);
};

class ClientVidAckMsg : public ClientAckMsg
{
public:
  ClientVidAckMsg(const json & msg);

  double ssim {};
  VideoFormat video_format;
};

class ClientAudAckMsg : public ClientAckMsg
{
public:
  ClientAudAckMsg(const json & msg);

  AudioFormat audio_format;
};

class ClientMsgParser
{
public:
  enum class Type {
    Unknown,
    Init,
    Info,  /* rebuffer, play, or timer */
    VideoAck,
    AudioAck
  };

  ClientMsgParser(const std::string & data);

  ClientInitMsg parse_client_init();
  ClientInfoMsg parse_client_info();
  ClientVidAckMsg parse_client_vidack();
  ClientAudAckMsg parse_client_audack();

  Type msg_type() const { return type_; }

private:
  json msg_;
  Type type_ {Type::Unknown};
};

#endif /* CLIENT_MESSAGE_HH */
