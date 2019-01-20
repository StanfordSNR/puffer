#include "mpc.hh"
#include "ws_client.hh"

using namespace std;

MPC::MPC(const WebSocketClient & client,
         const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{
  if (abr_config["max_lookahead_horizon"]) {
    max_lookahead_horizon_ = min(
      max_lookahead_horizon_,
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

  if (abr_name_ == "robust_mpc") {
    is_robust_ = true;
  }

  unit_buf_length_ = WebSocketClient::MAX_BUFFER_S / dis_buf_length_;

  for (size_t i = 0; i <= dis_buf_length_; i++) {
    real_buffer_[i] = i * unit_buf_length_;
  }
}

void MPC::video_chunk_acked(Chunk && c)
{
  double err = 0;

  if (is_robust_ and last_tp_pred_ > 0) {
    err = fabs(1 - last_tp_pred_ *  c.trans_time / c.size / 1000);
  }

  past_chunks_.push_back({c.ssim, c.size, c.trans_time, err});
  if (past_chunks_.size() > max_num_past_chunks_) {
    past_chunks_.pop_front();
  }
}

VideoFormat MPC::select_video_format()
{
  reinit();
  size_t ret_format = update_value(0, curr_buffer_, 0);
  return client_.channel()->vformats()[ret_format];
}

void MPC::reinit()
{
  curr_round_++;

  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  const unsigned int vduration = channel->vduration();
  const uint64_t next_ts = client_.next_vts().value();

  chunk_length_ = (double) vduration / channel->timescale();
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
    is_init_ = false;
    curr_ssims_[0][0] = ssim_db(past_chunks_.back().ssim);
  } else {
    is_init_ = true;
  }

  for (size_t i = 1; i <= lookahead_horizon_; i++) {
    for (size_t j = 0; j < num_formats_; j++) {
      try {
        curr_ssims_[i][j] = ssim_db(
            channel->vssim(vformats[j], next_ts + vduration * (i - 1)));
      } catch (const exception & e) {
        cerr << "Error occurs when getting the ssim of "
             << next_ts + vduration * (i - 1) << " " << vformats[j] << endl;
        curr_ssims_[i][j] = MIN_SSIM;
      }
    }
  }

  /* init curr_sending_time */
  size_t num_past_chunks = past_chunks_.size();
  auto it = past_chunks_.begin();
  double max_err = 0;

  for (size_t i = 1; it != past_chunks_.end(); it++, i++) {
    unit_sending_time_[i] = (double) it->trans_time / it->size / 1000;
    max_err = max(max_err, it->pred_err);
  }

  if (not is_robust_) {
    max_err = 0;
  }

  for (size_t i = 1; i <= lookahead_horizon_; i++) {
    double tmp = 0;
    for (size_t j = 0; j < num_past_chunks; j++) {
      tmp += unit_sending_time_[i + j];
    }

    if (num_past_chunks != 0) {
      double unit_st = tmp / num_past_chunks;

      if (i == 1) {
        last_tp_pred_ = 1 / unit_st;
      }

      unit_sending_time_[i + num_past_chunks] = unit_st * (1 + max_err);
    } else {
      /* set the sending time to be a default hight value */
      unit_sending_time_[i + num_past_chunks] = HIGH_SENDING_TIME;
    }

    const auto & data_map = channel->vdata(next_ts + vduration * (i - 1));

    for (size_t j = 0; j < num_formats_; j++) {
      try {
        curr_sending_time_[i][j] = get<1>(data_map.at(vformats[j]))
                                   * unit_sending_time_[i + num_past_chunks];
      } catch (const exception & e) {
        cerr << "Error occurs when getting the video size of "
             << next_ts + vduration * (i - 1) << " " << vformats[j] << endl;
        curr_sending_time_[i][j] = HIGH_SENDING_TIME;
      }
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
  double real_rebuffer = curr_sending_time_[i + 1][next_format]
                         - real_buffer_[curr_buffer];
  size_t next_buffer = discretize_buffer(max(0.0, -real_rebuffer) + chunk_length_);
  next_buffer = min(next_buffer, dis_buf_length_);

  if (is_init_ and i == 0) {
    return curr_ssims_[i][curr_format]
           - rebuffer_length_coeff_ * max(0.0, real_rebuffer)
           + get_value(i + 1, next_buffer, next_format);
  }
  return curr_ssims_[i][curr_format]
         - ssim_diff_coeff_ * fabs(curr_ssims_[i][curr_format]
                                   - curr_ssims_[i + 1][next_format])
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
  return (buf + unit_buf_length_ * 0.5) / unit_buf_length_;
}
