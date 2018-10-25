#ifndef WS_CLIENT_HH
#define WS_CLIENT_HH

#include <cstdint>
#include <optional>
#include <string>

#include "address.hh"
#include "channel.hh"
#include "media_formats.hh"

class MediaSegment
{
public:
  MediaSegment(const mmap_t & data, const std::optional<mmap_t> & init);

  void read(std::string & dst, const size_t n);
  size_t offset() { return offset_; }
  size_t length() { return length_; }
  bool done() { return offset_ == length_; }

private:
  std::optional<mmap_t> init_;
  mmap_t data_;
  size_t offset_;
  size_t length_;
};

class VideoSegment : public MediaSegment
{
public:
  VideoSegment(const VideoFormat & format,
               const mmap_t & data,
               const std::optional<mmap_t> & init);

  const VideoFormat & format() const { return format_; }

private:
  VideoFormat format_;
};

class AudioSegment : public MediaSegment
{
public:
  AudioSegment(const AudioFormat & format,
               const mmap_t & data,
               const std::optional<mmap_t> & init);

  const AudioFormat & format() const { return format_; }

private:
  AudioFormat format_;
};

class WebSocketClient
{
public:
  WebSocketClient(const uint64_t connection_id);

  void init(const std::string & channel,
            const uint64_t vts, const uint64_t ats);

  /* accessors */
  uint64_t connection_id() const { return connection_id_; }
  unsigned int init_id() const { return init_id_; }

  bool is_authenticated() const { return authenticated_; }
  std::string session_key() const { return session_key_; }
  std::string username() const { return username_; }
  std::string channel() const { return channel_; }

  std::string signature() const {
    return std::to_string(connection_id_) + "," + username_;
  }

  std::string browser() const { return browser_; }
  std::string os() const { return os_; }
  Address address() const { return address_; }
  uint16_t screen_height() const { return screen_height_; }
  uint16_t screen_width() const { return screen_width_; }

  bool is_format_capable(const VideoFormat & format) const;

  std::optional<uint64_t> next_vts() const { return next_vts_; }
  std::optional<uint64_t> next_ats() const { return next_ats_; }
  std::optional<VideoSegment> & next_vsegment() { return next_vsegment_; }
  std::optional<AudioSegment> & next_asegment() { return next_asegment_; }

  std::optional<uint64_t> client_next_vts() const { return client_next_vts_; }
  std::optional<uint64_t> client_next_ats() const { return client_next_ats_; }

  std::optional<uint64_t> video_in_flight() const;
  std::optional<uint64_t> audio_in_flight() const;

  double video_playback_buf() const { return video_playback_buf_; }
  double audio_playback_buf() const { return audio_playback_buf_; }

  std::optional<VideoFormat> curr_vq() const { return curr_vq_; }
  std::optional<AudioFormat> curr_aq() const { return curr_aq_; }

  bool is_rebuffering() const { return rebuffering_; }
  std::optional<double> curr_tput() const { return curr_tput_; }

  std::optional<time_t> get_last_msg_time() const { return last_msg_time_; }

  /* mutators */
  void set_authenticated(const bool authenticated) { authenticated_ = authenticated; }
  void set_session_key(const std::string & session_key) { session_key_ = session_key; }
  void set_username(const std::string & username) { username_ = username; }

  void set_browser(const std::string & browser) { browser_ = browser; }
  void set_os(const std::string & os) { os_ = os; }
  void set_address(const Address & address) { address_ = address; }
  void set_screen_height(const uint16_t screen_height) { screen_height_ = screen_height; }
  void set_screen_width(const uint16_t screen_width) { screen_width_ = screen_width; }

  void set_max_video_size(const std::vector<VideoFormat> & vfs);

  void set_next_vts(const uint64_t next_vts) { next_vts_ = next_vts; }
  void set_next_ats(const uint64_t next_ats) { next_ats_ = next_ats; }

  void set_next_vsegment(const VideoFormat & format,
                         const mmap_t & data,
                         const std::optional<mmap_t> & init);
  void clear_next_vsegment() { next_vsegment_.reset(); }
  void set_next_asegment(const AudioFormat & format,
                         const mmap_t & data,
                         const std::optional<mmap_t> & init);
  void clear_next_asegment() { next_asegment_.reset(); }

  void set_client_next_vts(const uint64_t vts) { client_next_vts_ = vts; }
  void set_client_next_ats(const uint64_t ats) { client_next_ats_ = ats; }

  void set_video_playback_buf(const double buf) { video_playback_buf_ = buf; }
  void set_audio_playback_buf(const double buf) { audio_playback_buf_ = buf; }

  void set_curr_vq(const VideoFormat & quality) { curr_vq_ = quality; }
  void set_curr_aq(const AudioFormat & quality) { curr_aq_ = quality; }

  void set_rebuffering(const bool rebuffering) { rebuffering_ = rebuffering; }
  void set_curr_tput(const double curr_tput) { curr_tput_ = curr_tput; }

  void set_last_msg_time(const time_t t) { last_msg_time_ = t; }

private:
  uint64_t connection_id_ {};

  /* incremented every time a new client-init received */
  unsigned int init_id_ {0};

  bool authenticated_ {false};
  std::string session_key_ {};
  std::string username_ {};
  std::string channel_ {};

  /* fields set in client-init */
  std::string browser_ {};
  std::string os_ {};
  Address address_ {};
  uint16_t screen_height_ {0};
  uint16_t screen_width_ {0};

  /* max video size that is sufficient considering client's screen size */
  uint16_t max_video_height_ {0};
  uint16_t max_video_width_ {0};

  /* segments and timestamps in the process of being sent */
  std::optional<uint64_t> next_vts_ {};
  std::optional<uint64_t> next_ats_ {};
  std::optional<VideoSegment> next_vsegment_ {};
  std::optional<AudioSegment> next_asegment_ {};

  std::optional<VideoFormat> curr_vq_ {};
  std::optional<AudioFormat> curr_aq_ {};

  double video_playback_buf_ {0};
  double audio_playback_buf_ {0};

  /* next video and audio timestamps requested from the client */
  std::optional<uint64_t> client_next_vts_ {};
  std::optional<uint64_t> client_next_ats_ {};

  bool rebuffering_ {false};
  std::optional<double> curr_tput_ {};

  std::optional<time_t> last_msg_time_ {};
};

#endif /* WS_CLIENT_HH */
