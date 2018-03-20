#ifndef CLIENT_MESSAGE_HH
#define CLIENT_MESSAGE_HH

#include <cstdint>
#include <string>
#include <optional>
#include <exception>
#include <memory>

#include "json.hpp"
using json = nlohmann::json;

class ClientInitMsg
{
public:
  std::optional<std::string> channel {};

  int player_width {};
  int player_height {};

  std::optional<uint64_t> next_vts {};
  std::optional<uint64_t> next_ats {};
};

class ClientInfoMsg
{
public:
  enum class PlayerEvent {
    Unknown,
    Timer,
    Rebuffer,
    CanPlay,
    AudioAck,
    VideoAck
  };

  enum class PlayerReadyState {
    HaveNothing = 0,
    HaveMetadata = 1,
    HaveCurrentData = 2,
    HaveFutureData = 3,
    HaveEnoughData = 4
  };

  PlayerEvent event {PlayerEvent::Unknown};

  /* Length of client's buffer in seconds */
  double video_buffer_len {};
  double audio_buffer_len {};

  /* Next segment the client is expecting */
  unsigned int next_video_timestamp {};
  unsigned int next_audio_timestamp {};

  int player_width {};
  int player_height {};

  PlayerReadyState player_ready_state {PlayerReadyState::HaveNothing};

  unsigned int init_id {};
};

class BadClientMsgException : public std::exception
{
public:
  explicit BadClientMsgException(const char * msg) : msg_(msg) {}
  explicit BadClientMsgException(const std::string & msg) : msg_(msg) {}
  virtual ~BadClientMsgException() throw () {}
  virtual const char * what() const throw () { return msg_.c_str(); }

protected:
  std::string msg_;
};

class ClientMsgParser
{
public:
  enum class Type {
    Unknown,
    Init,
    Info
  };

  ClientMsgParser(const std::string & data);

  Type msg_type() { return type_; }

  ClientInitMsg parse_init_msg();
  ClientInfoMsg parse_info_msg();

private:
  json msg_ {};
  Type type_ {Type::Unknown};
};

#endif /* CLIENT_MESSAGE_HH */
