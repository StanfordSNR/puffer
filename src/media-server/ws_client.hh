#ifndef WS_CLIENT_HH
#define WS_CLIENT_HH

#include <cstdint>
#include <optional>
#include <tuple>

class WebSocketClient
{
public:
  WebSocketClient(const uint64_t connection_id);

  void initialize(const string & channel, const uint64_t vts, 
                  const uint64_t ats);

  /* accessors */
  std::optional<std::string> channel() { return channel_; }
  std::optional<uint64_t> next_vts() { return next_vts_; }
  std::optional<uint64_t> next_ats() { return next_ats_; }
  double video_playback_buf() { return video_playback_buf_; }
  double audio_playback_buf() { return audio_playback_buf_; }

  std::optional<std::tuple<string, string>> curr_vq() { return curr_vq_; }
  std::optional<std::string> curr_aq() { return curr_aq_; }

  /* mutators */
  void set_next_vts(const uint64_t next_vts) { next_vts_ = next_vts; }
  void set_next_ats(const uint64_t next_ats) { next_ats_ = next_ats; }

  void set_audio_playback_buf(const double buf) { audio_playback_buf_ = buf; }
  void set_video_playback_buf(const double buf) { video_playback_buf_ = buf; }

  void set_curr_vq(const tuple<string, string> quality) { curr_vq_ = quality; }
  void set_curr_aq(const string & quality) { curr_aq_ = quality; }

private:
  uint64_t connection_id_;

  std::optional<std::string> channel_;

  std::optional<uint64_t> next_vts_;
  std::optional<uint64_t> next_ats_;

  std::optional<std::tuple<string, string>> curr_vq_;
  std::optional<std::string> curr_aq_;

  double video_playback_buf_;
  double audio_playback_buf_;
};

#endif /* WS_CLIENT_HH */
