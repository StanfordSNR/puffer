#include "ws_client.hh"

WebSocketClient::WebSocketClient(const uint64_t connection_id,
                                 const uint64_t next_vts,
                                 const uint64_t next_ats)
  : connection_id_(connection_id), next_vts_(next_vts), next_ats_(next_ats),
    playback_buf_(0)
{}
