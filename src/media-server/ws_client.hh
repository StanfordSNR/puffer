#ifndef WS_CLIENT_HH
#define WS_CLIENT_HH

#include <cstdint>
#include <optional>
#include <tuple>
#include <string>

#include "yaml.hh"

class WebSocketClient
{
public:
  WebSocketClient(const uint64_t connection_id);

  void init(const std::string & channel, const uint64_t vts, const uint64_t ats);

  /* accessors */
  uint64_t connection_id() const { return connection_id_; }
  std::optional<std::string> channel() const { return channel_; }

  std::optional<uint64_t> next_vts() const { return next_vts_; }
  std::optional<uint64_t> next_ats() const { return next_ats_; }

  double video_playback_buf() const { return video_playback_buf_; }
  double audio_playback_buf() const { return audio_playback_buf_; }

  std::optional<VideoFormat> curr_vq() const { return curr_vq_; }
  std::optional<AudioFormat> curr_aq() const { return curr_aq_; }

  std::optional<uint64_t> client_next_vts() const { return client_next_vts_; }
  std::optional<uint64_t> client_next_ats() const { return client_next_ats_; }

  /* mutators */
  void set_next_vts(const uint64_t next_vts) { next_vts_ = next_vts; }
  void set_next_ats(const uint64_t next_ats) { next_ats_ = next_ats; }

  void set_audio_playback_buf(const double buf) { audio_playback_buf_ = buf; }
  void set_video_playback_buf(const double buf) { video_playback_buf_ = buf; }

  void set_curr_vq(const VideoFormat quality) { curr_vq_ = quality; }
  void set_curr_aq(const AudioFormat & quality) { curr_aq_ = quality; }

  void set_client_next_vts(const uint64_t vts) { client_next_vts_ = vts; }
  void set_client_next_ats(const uint64_t ats) { client_next_ats_ = ats; }

private:
  uint64_t connection_id_;

  /* Fields set in init */
  std::optional<std::string> channel_;
  std::optional<uint64_t> next_vts_;
  std::optional<uint64_t> next_ats_;
  double video_playback_buf_;
  double audio_playback_buf_;

  std::optional<VideoFormat> curr_vq_;
  std::optional<AudioFormat> curr_aq_;

  std::optional<uint64_t> client_next_vts_;
  std::optional<uint64_t> client_next_ats_;
};

#endif /* WS_CLIENT_HH */
