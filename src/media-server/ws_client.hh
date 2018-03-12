#ifndef WS_CLIENT_HH
#define WS_CLIENT_HH

#include <cstdint>

class WebSocketClient
{
public:
  WebSocketClient(const uint64_t connection_id,
                  const uint64_t next_vts,
                  const uint64_t next_ats);

private:
  uint64_t connection_id_;

  uint64_t next_vts_;
  uint64_t next_ats_;

  int playback_buf_;
};

#endif /* WS_CLIENT_HH */
