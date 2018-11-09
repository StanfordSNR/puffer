#include "mpc.hh"
#include "ws_client.hh"

using namespace std;

static const double HIGH_SENDING_TIME = 10;

MPC::MPC(const WebSocketClient & client,
         const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{
  if (abr_config["max_lookahead_horizon"]) {
    max_lookahead_horizon_ = min(max_lookahead_horizon_,
                             abr_config["max_lookahead_horizon"].as<size_t>());
  }

  if (abr_config["dis_buf_length"]) {
    dis_buf_length_ = min(dis_buf_length_,
                          abr_config["dis_buf_length"].as<size_t>());
  }

  if (abr_config["rebuffer_length_coeff"]) {
    rebuffer_length_coeff_ = abr_config["rebuffer_length_coeff"].as<double>();
  }

  if (abr_config["ssim_diff_coeff"]) {
    ssim_diff_coeff_ = abr_config["ssim_diff_coeff"].as<double>();
  }

  unit_buf_length_ = WebSocketClient::MAX_BUFFER_S / dis_buf_length_;

  for (size_t i = 0; i <= dis_buf_length_; i++) {
    real_buffer_[i] = i * unit_buf_length_;
  }
}

void MPC::video_chunk_acked(const VideoFormat & format,
                            const double ssim,
                            const unsigned int size,
                            const uint64_t trans_time)
{
  past_chunks_.push_back({format, ssim, size, trans_time});
  if (past_chunks_.size() > max_num_past_chunks_) {
    past_chunks_.pop_front();
  }
}

VideoFormat MPC::select_video_format()
{
  reinit();
  size_t ret_format = update_value(0, curr_buffer_, curr_format_);
  return client_.channel()->vformats()[ret_format];
}

void MPC::reinit()
{
  curr_round_++;

  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  const unsigned int vduration = channel->vduration();
  const uint64_t curr_ts = *client_.next_vts() - vduration;

  chunk_length_ = (double) vduration / channel->timescale();
  num_formats_ = vformats.size();
  lookahead_horizon_ = min(max_lookahead_horizon_,
                       (*channel->vready_frontier() - curr_ts) / vduration);

  /* initialization failed if there is no ready chunk ahead */
  if (lookahead_horizon_ == 0) {
    throw runtime_error("no ready chunk ahead");
  }

  curr_buffer_ = discretize_buffer(client_.video_playback_buf());

  /* get the current format */
  auto curr_format = *client_.curr_vq();
  curr_format_ = 0;
  for (size_t i = 0; i < num_formats_; i++) {
    if (vformats[i] == curr_format) {
      curr_format_ = i;
      break;
    }
  }

  /* init curr_ssims */
  for (size_t i = 0; i <= lookahead_horizon_; i++) {
    for (size_t j = 0; j < num_formats_; j++) {
      curr_ssims_[i][j] = channel->vssim(vformats[j], curr_ts + vduration * i);
    }
  }

  /* init curr_sending_time */
  size_t num_past_chunks = past_chunks_.size();

  auto it = past_chunks_.begin();
  for (size_t i = 1; it != past_chunks_.end(); it++, i++) {
    unit_sending_time_[i] = (double) it->trans_time / it->size / 1000;
  }

  for (size_t i = 1; i <= lookahead_horizon_; i++) {
    double tmp = 0;
    for (size_t j = 0; j < num_past_chunks; j++) {
      tmp += unit_sending_time_[i + j];
    }

    if (num_past_chunks != 0) {
      unit_sending_time_[i + num_past_chunks] = tmp / num_past_chunks;
    } else {
      /* set the sending time to be a default hight value */
      unit_sending_time_[i + num_past_chunks] = HIGH_SENDING_TIME;
    }

    const auto & data_map = channel->vdata(curr_ts + vduration * i);

    for (size_t j = 0; j < num_formats_; j++) {
      curr_sending_time_[i][j] = get<1>(data_map.at(vformats[j]))
                                 * unit_sending_time_[i + num_past_chunks];
    }
  }
}

size_t MPC::update_value(size_t i, size_t curr_buffer, size_t curr_format)
{
  flag_[i][curr_buffer][curr_format] = curr_round_;

  if (i == lookahead_horizon_) {
    v_[i][curr_buffer][curr_format] = curr_ssims_[i][curr_format];
    return 0;
  }

  size_t best_next_format = num_formats_;
  double max_qvalue = 0;
  for (size_t next_format = 0; next_format < num_formats_; next_format++) {
    double qvalue = get_qvalue(i, curr_buffer, curr_format, next_format);
    if (best_next_format == num_formats_ or qvalue > max_qvalue) {
      max_qvalue = qvalue;
      best_next_format = next_format;
    }
  }
  v_[i][curr_buffer][curr_format] = max_qvalue;

  return best_next_format;
}

double MPC::get_qvalue(size_t i, size_t curr_buffer, size_t curr_format,
                       size_t next_format)
{
  double real_rebuffer = curr_sending_time_[i+1][next_format]
                         - real_buffer_[curr_buffer];
  double next_buffer = discretize_buffer(max(0.0, -real_rebuffer) + chunk_length_);
  return curr_ssims_[i][curr_format]
         - ssim_diff_coeff_ * fabs(curr_ssims_[i][curr_format]
                                   - curr_ssims_[i+1][next_format])
         - rebuffer_length_coeff_ * max(0.0, real_rebuffer)
         + get_value(i + 1, next_buffer, next_format);
}

double MPC::get_value(size_t i, size_t curr_buffer, size_t curr_format)
{
  if (flag_[i][curr_buffer][curr_format] != curr_round_) {
    update_value(i, curr_buffer, curr_format);
  }
  return v_[i][curr_buffer][curr_format];
}

size_t MPC::discretize_buffer(double buf)
{
  size_t dis_buffer = (buf + unit_buf_length_ * 0.5) / unit_buf_length_;
  return max((size_t)0, min(dis_buf_length_, dis_buffer));
}
