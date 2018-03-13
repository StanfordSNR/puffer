#ifndef WS_CLIENT_HH
#define WS_CLIENT_HH

#include <cstdint>

class WebSocketClient
{
public:
  WebSocketClient(const uint64_t connection_id,
                  const uint64_t next_vts,
                  const uint64_t next_ats);

  /* accessors */
  uint64_t next_vts() { return next_vts_; }
  uint64_t next_ats() { return next_ats_; }
  int playback_buf() { return playback_buf_; }

  /* mutators */
  void set_next_vts(const uint64_t next_vts) { next_vts_ = next_vts; }
  void set_next_ats(const uint64_t next_ats) { next_ats_ = next_ats; }
  void set_playback_buf(const int buf) { playback_buf_ = buf; }

private:
  uint64_t connection_id_;

  uint64_t next_vts_;
  uint64_t next_ats_;

  int playback_buf_;
};

#endif /* WS_CLIENT_HH */
