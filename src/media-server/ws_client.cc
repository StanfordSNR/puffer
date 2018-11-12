#include "ws_client.hh"
#include "linear_bba.hh"
#include "mpc.hh"

using namespace std;

WebSocketClient::WebSocketClient(const uint64_t connection_id,
                                 const string & abr_name,
                                 const YAML::Node & abr_config)
  : connection_id_(connection_id), abr_name_(abr_name),
    abr_config_(abr_config), channel_()
{
  init_abr_algo();
}

void WebSocketClient::init(const shared_ptr<Channel> & channel,
                           const uint64_t init_vts, const uint64_t init_ats)
{
  channel_ = channel;
  next_vts_ = init_vts;
  next_ats_ = init_ats;

  curr_vq_.reset();
  curr_aq_.reset();

  video_playback_buf_ = 0;
  audio_playback_buf_ = 0;

  client_next_vts_ = init_vts;
  client_next_ats_ = init_ats;

  rebuffering_ = false;
  last_video_send_ts_.reset();

  /* reset the ABR algorithm if WebSocketClient is (re)inited */
  init_abr_algo();
}

void WebSocketClient::set_max_video_size(const std::vector<VideoFormat> & vfs)
{
  max_video_height_ = 0;
  max_video_width_ = 0;

  for (const auto & vf : vfs) {
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

void WebSocketClient::video_chunk_acked(const VideoFormat & format,
                                        const double ssim,
                                        const unsigned int chunk_size,
                                        const uint64_t transmission_time)
{
  abr_algo_->video_chunk_acked(format, ssim, chunk_size, transmission_time);
}

VideoFormat WebSocketClient::select_video_format()
{
  return abr_algo_->select_video_format();
}

AudioFormat WebSocketClient::select_audio_format()
{
  double buf = min(max(audio_playback_buf_, 0.0), MAX_BUFFER_S);

  const auto & channel = channel_.lock();
  const auto & aformats = channel->aformats();
  size_t aformats_cnt = aformats.size();

  uint64_t next_ats = next_ats_.value();
  const auto & data_map = channel->adata(next_ats);

  /* get max and min chunk size for the next audio ts */
  size_t max_size = 0, min_size = SIZE_MAX;
  size_t max_idx = aformats_cnt, min_idx = aformats_cnt;

  for (size_t i = 0; i < aformats_cnt; i++) {
    const auto & af = aformats[i];

    size_t chunk_size = get<1>(data_map.at(af));
    if (chunk_size <= 0) continue;

    if (chunk_size > max_size) {
      max_size = chunk_size;
      max_idx = i;
    }

    if (chunk_size < min_size) {
      min_size = chunk_size;
      min_idx = i;
    }
  }

  assert(max_idx < aformats_cnt);
  assert(min_idx < aformats_cnt);

  if (buf >= 0.8 * MAX_BUFFER_S) {
    return aformats[max_idx];
  } else if (buf <= 0.2 * MAX_BUFFER_S) {
    return aformats[min_idx];
  }

  /* pick the largest chunk with size <= max_serve_size */
  double max_serve_size = ceil(buf * (max_size - min_size) / MAX_BUFFER_S
                               + min_size);
  size_t biggest_chunk_size = 0;
  size_t ret_idx = aformats_cnt;

  for (size_t i = 0; i < aformats_cnt; i++) {
    const auto & af = aformats[i];

    size_t chunk_size = get<1>(data_map.at(af));
    if (chunk_size <= 0 or chunk_size > max_serve_size) {
      continue;
    }

    if (chunk_size > biggest_chunk_size) {
      biggest_chunk_size = chunk_size;
      ret_idx = i;
    }
  }

  assert(ret_idx < aformats_cnt);
  return aformats[ret_idx];
}

void WebSocketClient::init_abr_algo()
{
  if (abr_name_ == "linear_bba") {
    abr_algo_ = make_unique<LinearBBA>(*this, abr_name_, abr_config_);
  } else if (abr_name_ == "mpc") {
    abr_algo_ = make_unique<MPC>(*this, abr_name_, abr_config_);
  } else {
    throw runtime_error("undefined ABR algorithm");
  }
}
