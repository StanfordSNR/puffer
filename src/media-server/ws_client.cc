#include "ws_client.hh"

using namespace std;

WebSocketClient::WebSocketClient(const uint64_t connection_id)
  : connection_id_(connection_id),
    channel_(), next_vts_(), next_ats_(),
    video_playback_buf_(), audio_playback_buf_(),
    curr_vq_(), curr_aq_(),
    client_next_vts_(), client_next_ats_(),
    init_id_(0)
{}

void WebSocketClient::init(const string & channel,
                           const uint64_t vts, const uint64_t ats)
{
  channel_ = channel;
  next_vts_ = vts;
  next_ats_ = ats;
  video_playback_buf_ = 0;
  audio_playback_buf_ = 0;

  curr_vq_.reset();
  curr_aq_.reset();

  client_next_vts_ = vts;
  client_next_ats_ = ats;

  init_id_++;
}
