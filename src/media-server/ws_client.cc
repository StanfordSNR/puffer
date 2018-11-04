#include "ws_client.hh"

#include "linear_bba.hh"

using namespace std;

WebSocketClient::WebSocketClient(const uint64_t connection_id)
{
  connection_id_ = connection_id;
}

void WebSocketClient::init(const string & channel,
                           const uint64_t vts, const uint64_t ats)
{
  channel_ = channel;
  init_id_++;

  next_vts_ = vts;
  next_ats_ = ats;

  curr_vq_.reset();
  curr_aq_.reset();

  video_playback_buf_ = 0;
  audio_playback_buf_ = 0;

  client_next_vts_ = vts;
  client_next_ats_ = ats;

  rebuffering_ = false;
}

void WebSocketClient::set_abr_algo(const string & abr_name,
                                   const YAML::Node & abr_config)
{
  if (abr_name == "linear_bba") {
    abr_algo_ = make_unique<LinearBBA>(abr_name, abr_config);
  }
}

void WebSocketClient::set_max_video_size(const std::vector<VideoFormat> & vfs)
{
  max_video_height_ = 0;
  max_video_width_ = 0;

  for (auto & vf : vfs) {
    /* set max video height and width according to the given video formats */
    if (screen_height_ and vf.height >= screen_height_ and
        (not max_video_height_ or vf.height < max_video_height_) ) {
        max_video_height_ = vf.height;
    }

    if (screen_width_ and vf.width >= screen_width_ and
        (not max_video_width_ or vf.width < max_video_width_) ) {
        max_video_width_ = vf.width;
    }
  }
}

bool WebSocketClient::is_format_capable(const VideoFormat & format) const
{
  return (not max_video_width_ or format.width <= max_video_width_) and
         (not max_video_height_ or format.height <= max_video_height_);
}

optional<uint64_t> WebSocketClient::video_in_flight() const
{
  if (not next_vts_ or not client_next_vts_ or
      *next_vts_ < *client_next_vts_) {
    return nullopt;
  }

  return *next_vts_ - *client_next_vts_;
}

optional<uint64_t> WebSocketClient::audio_in_flight() const
{
  if (not next_ats_ or not client_next_ats_ or
      *next_ats_ < *client_next_ats_) {
    return nullopt;
  }

  return *next_ats_ - *client_next_ats_;
}
