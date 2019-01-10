#include "puffer.hh"
#include "ws_client.hh"

using namespace std;

Puffer::Puffer(const WebSocketClient & client,
         const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{
  if (abr_config["max_lookahead_horizon"]) {
    max_lookahead_horizon_ = min(
      max_lookahead_horizon_,
      abr_config["max_lookahead_horizon"].as<size_t>());
  }

  if (abr_config["rebuffer_length_coeff"]) {
    rebuffer_length_coeff_ = abr_config["rebuffer_length_coeff"].as<double>();
  }

  if (abr_config["ssim_diff_coeff"]) {
    ssim_diff_coeff_ = abr_config["ssim_diff_coeff"].as<double>();
  }

  dis_buf_length_  = min(dis_buf_length_ ,
                         discretize_buffer(WebSocketClient::MAX_BUFFER_S));
}

void Puffer::video_chunk_acked(const VideoFormat & format,
                            const double ssim,
                            const unsigned int size,
                            const uint64_t trans_time)
{
  past_chunks_.push_back({format, ssim, size, trans_time});
  if (past_chunks_.size() > max_num_past_chunks_) {
    past_chunks_.pop_front();
  }
}

VideoFormat Puffer::select_video_format()
{
  reinit();
  size_t ret_format = update_value(0, curr_buffer_, 0);
  return client_.channel()->vformats()[ret_format];
}

void Puffer::reinit()
{
  curr_round_++;

  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  const unsigned int vduration = channel->vduration();
  const uint64_t next_ts = client_.next_vts().value();

  dis_chunk_length_ = discretize_buffer((double) vduration / channel->timescale());
  num_formats_ = vformats.size();

  /* initialization failed if there is no ready chunk ahead */
  if (channel->vready_frontier().value() < next_ts || num_formats_ == 0) {
    throw runtime_error("no ready chunk ahead");
  }

  lookahead_horizon_ = min(
    max_lookahead_horizon_,
    (channel->vready_frontier().value() - next_ts) / vduration + 1);

  curr_buffer_ = min(dis_buf_length_,
                     discretize_buffer(client_.video_playback_buf()));

  /* init curr_ssims */
  if (past_chunks_.size() > 0) {
    curr_ssims_[0][0] = past_chunks_.back().ssim;
  } else {
    curr_ssims_[0][0] = 0;
  }

  for (size_t i = 1; i <= dis_buf_length_; i++) {
    for (size_t j = 0; j < num_formats_; j++) {
      try {
        curr_ssims_[i][j] = channel->vssim(vformats[j],
                                           next_ts + vduration * (i - 1));
      } catch (const exception & e) {
        cerr << "Error occurs when getting the ssim of "
             << next_ts + vduration * (i - 1) << " " << vformats[j] << endl;
        curr_ssims_[i][j] = 0;
      }
    }
  }

  /* init sending time */
  reinit_sending_time();
}

size_t Puffer::update_value(size_t i, size_t curr_buffer, size_t curr_format)
{
  flag_[i][curr_buffer][curr_format] = curr_round_;

  if (i == lookahead_horizon_) {
    v_[i][curr_buffer][curr_format] = curr_ssims_[i][curr_format];
    return 0;
  }

  size_t best_next_format = num_formats_;
  double max_qvalue = 0;
  for (size_t next_format = 0; next_format < num_formats_; next_format++) {
    if (is_ban_[i + 1][next_format] == true) {
      continue;
    }

    double qvalue = get_qvalue(i, curr_buffer, curr_format, next_format);
    if (best_next_format == num_formats_ or qvalue > max_qvalue) {
      max_qvalue = qvalue;
      best_next_format = next_format;
    }
  }
  v_[i][curr_buffer][curr_format] = max_qvalue;

  return best_next_format;
}

double Puffer::get_qvalue(size_t i, size_t curr_buffer, size_t curr_format,
                       size_t next_format)
{
  assert(is_ban_[i + 1][next_format] == false);

  double ans = curr_ssims_[i][curr_format] - ssim_diff_coeff_
               * fabs(curr_ssims_[i][curr_format] - curr_ssims_[i + 1][next_format]);

  for (size_t st = 0; st <= dis_sending_time_; st++) {
    if (sending_time_prob_[i + 1][next_format][st] < st_prob_eps_) {
      continue;
    }

    int rebuffer = st - curr_buffer;
    size_t next_buffer = min(max(-rebuffer, 0) + dis_chunk_length_,
                             dis_buf_length_);
    rebuffer = max(rebuffer, 0);
    double real_rebuffer = rebuffer * unit_buf_length_;

    if (curr_buffer - st == 0) {
      real_rebuffer = rebuffer * unit_buf_length_ * 0.25;
    }

    ans += sending_time_prob_[i+1][next_format][st]
           * (get_value(i + 1, next_buffer, next_format)
              - rebuffer_length_coeff_ * real_rebuffer);
  }

  return ans;
}

double Puffer::get_value(size_t i, size_t curr_buffer, size_t curr_format)
{
  if (flag_[i][curr_buffer][curr_format] != curr_round_) {
    update_value(i, curr_buffer, curr_format);
  }
  return v_[i][curr_buffer][curr_format];
}

size_t Puffer::discretize_buffer(double buf)
{
  return (buf + unit_buf_length_ * 0.5) / unit_buf_length_;
}
