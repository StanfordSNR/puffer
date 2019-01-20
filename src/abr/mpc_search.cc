#include "mpc_search.hh"
#include "ws_client.hh"

using namespace std;

MPCSearch::MPCSearch(const WebSocketClient & client,
                     const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{
  if (abr_config["max_lookahead_horizon"]) {
    max_lookahead_horizon_ = min(
      max_lookahead_horizon_,
      abr_config["max_lookahead_horizon"].as<size_t>());
  }

  if (abr_config["dis_buf_length"]) {
    is_discrete_buf_ = true;
    dis_buf_length_ = min(dis_buf_length_,
                          abr_config["dis_buf_length"].as<size_t>());
  }

  if (abr_config["rebuffer_length_coeff"]) {
    rebuffer_length_coeff_ = abr_config["rebuffer_length_coeff"].as<double>();
  }

  if (abr_config["ssim_diff_coeff"]) {
    ssim_diff_coeff_ = abr_config["ssim_diff_coeff"].as<double>();
  }

  if (is_discrete_buf_) {
    unit_buf_length_ = WebSocketClient::MAX_BUFFER_S / dis_buf_length_;
    for (size_t i = 0; i <= dis_buf_length_; i++) {
      real_buffer_[i] = i * unit_buf_length_;
    }
  }
}

void MPCSearch::video_chunk_acked(Chunk && c)
{
  past_chunks_.push_back(c);
  if (past_chunks_.size() > max_num_past_chunks_) {
    past_chunks_.pop_front();
  }
}

VideoFormat MPCSearch::select_video_format()
{
  reinit();

  size_t best_next_format = num_formats_;
  double max_qvalue = 0;
  for (size_t next_format = 0; next_format < num_formats_; next_format++) {
    double qvalue = get_qvalue(0, curr_buffer_, 0, next_format);
    if (best_next_format == num_formats_ or qvalue > max_qvalue) {
      max_qvalue = qvalue;
      best_next_format = next_format;
    }
  }
  return client_.channel()->vformats()[best_next_format];
}

void MPCSearch::reinit()
{
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

  curr_buffer_ = min(WebSocketClient::MAX_BUFFER_S,
                     client_.video_playback_buf());
  if (is_discrete_buf_) {
    curr_buffer_ = discretize_buffer(curr_buffer_);
  }

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

double MPCSearch::get_qvalue(size_t i, double curr_buffer, size_t curr_format,
                             size_t next_format)
{
  double real_rebuffer = curr_sending_time_[i + 1][next_format] - curr_buffer;
  double next_buffer = min(WebSocketClient::MAX_BUFFER_S,
                           max(0.0, -real_rebuffer) + chunk_length_);
  if (is_discrete_buf_) {
    next_buffer = discretize_buffer(next_buffer);
  }

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

double MPCSearch::get_value(size_t i, double curr_buffer, size_t curr_format)
{
  if (i == lookahead_horizon_) {
    return curr_ssims_[i][curr_format];
  }

  double max_qvalue = 0;
  for (size_t next_format = 0; next_format < num_formats_; next_format++) {
    double qvalue = get_qvalue(i, curr_buffer, curr_format, next_format);
    if (next_format == 0) {
      max_qvalue = qvalue;
    }
    max_qvalue = max(max_qvalue, qvalue);
  }
  return max_qvalue;
}

double MPCSearch::discretize_buffer(double buf)
{
  size_t dis_buf = (buf + unit_buf_length_ * 0.5) / unit_buf_length_;
  return dis_buf * unit_buf_length_;
}
