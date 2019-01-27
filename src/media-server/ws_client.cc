#include "ws_client.hh"
#include "linear_bba.hh"
#include "mpc.hh"
#include "mpc_search.hh"
#include "pensieve.hh"
#include "puffer_raw.hh"
#include "puffer_ttp.hh"
#include "timestamp.hh"
#include "exception.hh"

using namespace std;

static constexpr double LOWER_RESERVOIR = 0.1;
static constexpr double UPPER_RESERVOIR = 0.9;

WebSocketClient::WebSocketClient(const uint64_t connection_id,
                                 const string & abr_name,
                                 const YAML::Node & abr_config)
  : connection_id_(connection_id), abr_name_(abr_name),
    abr_config_(abr_config), channel_(), last_msg_recv_ts_(timestamp_ms())
{
  init_abr_algo();
}

void WebSocketClient::reset_helper()
{
  video_playback_buf_ = 0;
  audio_playback_buf_ = 0;

  startup_delay_.reset();
  cum_rebuffer_ = 0;

  curr_vformat_.reset();
  curr_aformat_.reset();

  last_video_send_ts_.reset();
  tcp_info_.reset();
}

void WebSocketClient::init_channel(const shared_ptr<Channel> & channel,
                                   const uint64_t init_vts,
                                   const uint64_t init_ats)
{
  channel_ = channel;
  next_vts_ = init_vts;
  next_ats_ = init_ats;
  client_next_vts_ = init_vts;
  client_next_ats_ = init_ats;

  reset_helper();
}

void WebSocketClient::reset_channel()
{
  channel_.reset();
  next_vts_.reset();
  next_ats_.reset();
  client_next_vts_.reset();
  client_next_ats_.reset();

  reset_helper();
}

bool WebSocketClient::is_channel_initialized() const
{
  return (channel_.lock() and next_vts_ and next_ats_ and
          client_next_vts_ and client_next_ats_);
}

void WebSocketClient::set_screen_size(const uint16_t screen_width,
                                      const uint16_t screen_height)
{
  screen_width_ = screen_width;
  screen_height_ = screen_height;
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
  try {
    const auto & ti = tcp_info_.value();

    abr_algo_->video_chunk_acked({
      format, ssim, chunk_size, transmission_time,
      ti.cwnd, ti.in_flight, ti.min_rtt, ti.rtt, ti.delivery_rate
    });
  } catch (const exception & e) {
    print_exception("video_chunk_acked", e);
    throw runtime_error("Error: video_chunk_acked failed with " + abr_name_);
  }
}

VideoFormat WebSocketClient::select_video_format()
{
  try {
    return abr_algo_->select_video_format();
  } catch (const exception & e) {
    print_exception("select_video_format", e);
    throw runtime_error("Error: select_video_format failed with " + abr_name_);
  }
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

  if (buf >= UPPER_RESERVOIR * MAX_BUFFER_S) {
    return aformats[max_idx];
  } else if (buf <= LOWER_RESERVOIR * MAX_BUFFER_S) {
    return aformats[min_idx];
  }

  /* pick the largest chunk with size <= max_serve_size */
  double slope = (max_size - min_size) /
                 ((UPPER_RESERVOIR - LOWER_RESERVOIR) * MAX_BUFFER_S);
  double max_serve_size = min_size +
                          slope * (buf - LOWER_RESERVOIR * MAX_BUFFER_S);
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
  } else if (abr_name_ == "robust_mpc") {
    abr_algo_ = make_unique<MPC>(*this, abr_name_, abr_config_);
  } else if (abr_name_ == "mpc_search") {
    abr_algo_ = make_unique<MPCSearch>(*this, abr_name_, abr_config_);
  } else if (abr_name_ == "pensieve") {
    abr_algo_ = make_unique<Pensieve>(*this, abr_name_, abr_config_);
  } else if (abr_name_ == "puffer_raw") {
    abr_algo_ = make_unique<PufferRaw>(*this, abr_name_, abr_config_);
  } else if (abr_name_ == "puffer_ttp") {
    abr_algo_ = make_unique<PufferTTP>(*this, abr_name_, abr_config_);
  } else if (abr_name_ == "puffer_ttp_no_tcp_info") {
    abr_algo_ = make_unique<PufferTTP>(*this, abr_name_, abr_config_);
  } else if (abr_name_ == "puffer_ttp_mle") {
    abr_algo_ = make_unique<PufferTTP>(*this, abr_name_, abr_config_);
  } else {
    throw runtime_error("undefined ABR algorithm");
  }
}
