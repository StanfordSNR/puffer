#ifndef WS_CLIENT_HH
#define WS_CLIENT_HH

#include <cstdint>
#include <optional>
#include <string>
#include <memory>

#include "address.hh"
#include "channel.hh"
#include "server_message.hh"
#include "media_formats.hh"
#include "yaml.hh"
#include "socket.hh"

class ABRAlgo;

class WebSocketClient
{
public:
  WebSocketClient(const uint64_t connection_id,
                  const std::string & abr_name,
                  const YAML::Node & abr_config);

  /* forbid copying or move assigning WebSocketClient */
  WebSocketClient(const WebSocketClient & other) = delete;
  const WebSocketClient & operator=(const WebSocketClient & other) = delete;

  /* forbid moving or copy assigning WebSocketClient */
  WebSocketClient(WebSocketClient && other) = delete;
  WebSocketClient & operator=(WebSocketClient && other) = delete;

  /* start streaming the requested channel to client */
  void init_channel(const std::shared_ptr<Channel> & channel,
                    const uint64_t init_vts, const uint64_t init_ats);

  bool is_channel_initialized() const;

  /* reset the client and wait for client-init */
  void reset_channel();

  /* accessors */
  uint64_t connection_id() const { return connection_id_; }

  std::shared_ptr<Channel> channel() const { return channel_.lock(); }

  std::optional<unsigned int> init_id() const { return init_id_; }
  std::optional<unsigned int> first_init_id() const { return first_init_id_; }

  bool is_authenticated() const { return authenticated_; }
  std::string session_key() const { return session_key_; }
  std::string username() const { return username_; }

  std::string signature() const {
    return std::to_string(connection_id_) + "," + username_;
  }

  std::string browser() const { return browser_; }
  std::string os() const { return os_; }
  Address address() const { return address_; }

  uint16_t screen_width() const { return screen_width_; }
  uint16_t screen_height() const { return screen_height_; }

  std::optional<uint64_t> next_vts() const { return next_vts_; }
  std::optional<uint64_t> next_ats() const { return next_ats_; }

  std::optional<uint64_t> client_next_vts() const { return client_next_vts_; }
  std::optional<uint64_t> client_next_ats() const { return client_next_ats_; }

  std::optional<uint64_t> video_in_flight() const;
  std::optional<uint64_t> audio_in_flight() const;

  double video_playback_buf() const { return video_playback_buf_; }
  double audio_playback_buf() const { return audio_playback_buf_; }

  std::optional<double> startup_delay() const { return startup_delay_; }
  double cum_rebuffer() const { return cum_rebuffer_; }

  std::optional<VideoFormat> curr_vformat() const { return curr_vformat_; }
  std::optional<AudioFormat> curr_aformat() const { return curr_aformat_; }

  uint64_t last_msg_recv_ts() const { return last_msg_recv_ts_; }

  std::optional<uint64_t> last_video_send_ts() const { return last_video_send_ts_; }
  std::optional<TCPInfo> tcp_info() const { return tcp_info_; }

  /* mutators */
  void set_init_id(const unsigned int init_id);

  void set_authenticated(const bool authenticated) { authenticated_ = authenticated; }
  void set_session_key(const std::string & session_key) { session_key_ = session_key; }
  void set_username(const std::string & username) { username_ = username; }

  void set_browser(const std::string & browser) { browser_ = browser; }
  void set_os(const std::string & os) { os_ = os; }
  void set_address(const Address & address) { address_ = address; }

  void set_screen_size(const uint16_t screen_width, const uint16_t screen_height);

  void set_next_vts(const uint64_t next_vts) { next_vts_ = next_vts; }
  void set_next_ats(const uint64_t next_ats) { next_ats_ = next_ats; }

  void set_client_next_vts(const uint64_t vts) { client_next_vts_ = vts; }
  void set_client_next_ats(const uint64_t ats) { client_next_ats_ = ats; }

  void set_video_playback_buf(const double buf) { video_playback_buf_ = buf; }
  void set_audio_playback_buf(const double buf) { audio_playback_buf_ = buf; }

  void set_startup_delay(const double delay) { startup_delay_ = delay; }
  void set_cum_rebuffer(const double cum_rebuf) { cum_rebuffer_ = cum_rebuf; }

  void set_curr_vformat(const VideoFormat & format) { curr_vformat_ = format; }
  void set_curr_aformat(const AudioFormat & format) { curr_aformat_ = format; }

  void set_last_msg_recv_ts(uint64_t recv_ts) { last_msg_recv_ts_ = recv_ts; }

  void set_last_video_send_ts(const std::optional<uint64_t> send_ts) { last_video_send_ts_ = send_ts; }
  void set_tcp_info(const std::optional<TCPInfo> tcp_info) { tcp_info_ = tcp_info; }

  /* ABR related */
  void video_chunk_acked(const VideoFormat & format,
                         const double ssim,
                         const unsigned int chunk_size,
                         const uint64_t transmission_time);
  VideoFormat select_video_format();
  AudioFormat select_audio_format();

  static constexpr double MAX_BUFFER_S = 15.0;  /* seconds */

private:
  uint64_t connection_id_;

  /* ABR algorithm */
  std::string abr_name_;
  YAML::Node abr_config_;
  std::unique_ptr<ABRAlgo> abr_algo_ {nullptr};

  /* WebSocketClient has no interest in managing the ownership of channel */
  std::weak_ptr<Channel> channel_;

  /* timestamp of the last message received from client */
  uint64_t last_msg_recv_ts_;

  /* set to the init_id in the most recently received client-init */
  std::optional<unsigned int> init_id_ {};
  std::optional<unsigned int> first_init_id_ {};

  bool authenticated_ {false};
  std::string session_key_ {};
  std::string username_ {};

  /* fields set in client-init */
  std::string browser_ {};
  std::string os_ {};
  Address address_ {};

  uint16_t screen_width_ {0xFFFF};
  uint16_t screen_height_ {0xFFFF};

  /* chunk timestamps in the process of being sent */
  std::optional<uint64_t> next_vts_ {};
  std::optional<uint64_t> next_ats_ {};

  /* next video and audio timestamps requested from the client */
  std::optional<uint64_t> client_next_vts_ {};
  std::optional<uint64_t> client_next_ats_ {};

  /* playback buffer in seconds */
  double video_playback_buf_ {0};
  double audio_playback_buf_ {0};

  std::optional<double> startup_delay_ {};

  /* cumulative rebuffering time, including startup_delay_ */
  double cum_rebuffer_ {};

  /* current video and audio formats */
  std::optional<VideoFormat> curr_vformat_ {};
  std::optional<AudioFormat> curr_aformat_ {};

  /* sending time of last video chunk */
  std::optional<uint64_t> last_video_send_ts_ {};
  /* TCP info before sending a video chunk */
  std::optional<TCPInfo> tcp_info_ {};

  /* (re)instantiate abr_algo_ */
  void init_abr_algo();

  void reset_helper();
};

#endif /* WS_CLIENT_HH */
