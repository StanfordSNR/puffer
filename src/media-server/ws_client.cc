#include "ws_client.hh"

// TODO: fix this constructor
WebSocketClient::WebSocketClient(const uint64_t connection_id)
  : connection_id_(connection_id),
    channel_(),
    next_vts_(), next_ats_(),
    curr_vq_(), curr_aq_(),
{
  video_playback_buf_ = 0;
  audio_playback_buf_ = 0;
}

WebSocketClient::initialize(const string & channel, const uint64_t vts,
                            const uint64_t ats)
{
  channel_ = channel;
  
  next_vts_ = vts;
  next_ats_ = ats;

  curr_vq_.reset();
  curr_aq_.reset();

  video_playback_buf_ = 0;
  audio_playback_buf_ = 0;
}