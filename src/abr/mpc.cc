#include "mpc.hh"
#include "exception.hh"

#include <cmath>
#include <iostream>

using namespace std;

MPCAlgo::MPCAlgo(const double max_buf_length,
                 const size_t max_front_horizon,
                 const size_t dis_buf_length,
                 const double lambda,
                 const double mu)
{
  max_front_horizon_ = max_front_horizon;
  dis_buf_length_ = dis_buf_length;
  unit_buf_length_ = max_buf_length / dis_buf_length;
  lambda_ = lambda;
  mu_ = mu;

  for (size_t i = 0; i <= dis_buf_length_; i++) {
    real_buf_[i] = i * unit_buf_length_;
  }
}

void MPCAlgo::print_info()
{
  cerr << "front_horizon:" << front_horizon_ << endl;
  cerr << "chunk_length:" << chunk_length_ << endl;
  cerr << "dis_buf_length:" << dis_buf_length_ << endl;
  cerr << "unit_buf_length:" << unit_buf_length_ << endl;
  cerr << "pre_horizon:" << pre_horizon_ << endl;
  cerr << "num_formats:" << num_formats_ << endl;
  cerr << "front_horizon:" << front_horizon_ << endl;
  cerr << "lambda:" << lambda_ << endl;
  cerr << "mu:" << mu_ << endl;
  cerr << "cur_buf:" << cur_buf_ << endl;
  cerr << "cur_qua:" << cur_qua_ << endl;

  cerr << "cur_ssim:" << endl;
  for (size_t i = 0; i <= front_horizon_; i++) {
    for (size_t j = 0; j < num_formats_; j++) {
      cerr << cur_ssim_[i][j] << " ";
    }
    cerr << endl;
  }

  cerr << "next_dltime:" << endl;
  for (size_t i = 0; i < front_horizon_; i++) {
    for (size_t j = 0; j < num_formats_; j++) {
      cerr << next_sending_time_[i][j] << " ";
    }
    cerr << endl;
  }
}

bool MPCAlgo::is_ready(const WebSocketClient & client, const Channel & channel)
{
  if (not (channel.vready_frontier() and client.next_vts() and
      client.curr_vq())) {
    return 0;
  }

  if (*channel.vready_frontier() < client.next_vts() or
      client.last_dltimes().size() == 0) {
    return 0;
  }

  return 1;
}

const VideoFormat & MPCAlgo::select_video_quality(
  const WebSocketClient & client,
  const Channel & channel)
{
  reinit(client, channel);
  size_t ret_quality = update_value(0, cur_buf_, cur_qua_);
  return channel.vformats()[ret_quality];
}

void MPCAlgo::reinit(const WebSocketClient & client, const Channel & channel)
{
  cnt++;

  const auto & vformats = channel.vformats();
  const unsigned int vduration = channel.vduration();
  const uint64_t cur_ts = *client.next_vts() - vduration;

  chunk_length_ = (double) vduration / channel.timescale();
  num_formats_ = vformats.size();
  front_horizon_ = min(max_front_horizon_,
                       (*channel.vready_frontier() - cur_ts) / vduration);

  /* initial failed if there is no ready chunk ahead */
  if (front_horizon_ == 0) {
    throw runtime_error("no ready chunk ahead");
  }

  cur_buf_ = dis_buf(client.video_playback_buf());

  /* get the current format */
  auto cur_format = *client.curr_vq();
  cur_qua_ = num_formats_;
  for (size_t i = 0; i < num_formats_; i++) {
    if (vformats[i] == cur_format) {
      cur_qua_ = i;
      break;
    }
  }

  /* initial failed if the current format doesn't exists */
  if (cur_qua_ == num_formats_) {
    throw runtime_error("current format is invalid");
  }

  /* init cur_ssim */
  for (size_t i = 0; i <= front_horizon_; i++) {
    for (size_t j = 0; j < num_formats_; j++) {
      cur_ssim_[i][j] = channel.vssim(vformats[j], cur_ts + vduration * i);
    }
  }

  /* init next_sending_time */
  const auto & pre_sending_times = client.last_dltimes();
  pre_horizon_ = pre_sending_times.size();

  auto it = pre_sending_times.begin();
  for (size_t i = 0; it != pre_sending_times.end(); it++, i++) {
    unit_sending_time_[i] = (double) it->first / it->second / 1000;
  }

  for (size_t i = 0; i < front_horizon_; i++) {
    double tmp = 0;
    for (size_t j = 0; j < pre_horizon_; j++) {
      tmp += unit_sending_time_[i + j];
    }
    unit_sending_time_[i + pre_horizon_] = tmp / pre_horizon_;
    const auto & data_map = channel.vdata(cur_ts + vduration * i);

    for (size_t j = 0; j < num_formats_; j++) {
      next_sending_time_[i][j] = get<1>(data_map.at(vformats[j]))
                                 * unit_sending_time_[i + pre_horizon_];
    }
  }
}

size_t MPCAlgo::update_value(size_t i, size_t cur_buf, size_t cur_qua)
{
  flag_[i][cur_buf][cur_qua] = cnt;
  if (i == front_horizon_) {
    v_[i][cur_buf][cur_qua] = cur_ssim_[i][cur_qua];
    return -1;
  }
  size_t best_next_qua = num_formats_;
  double max_tmp = 0;
  for (size_t next_qua = 0; next_qua < num_formats_; next_qua++) {
    double tmp = get_qvalue(i, cur_buf, cur_qua, next_qua);
    if (best_next_qua == num_formats_ or tmp > max_tmp) {
      max_tmp = tmp;
      best_next_qua = next_qua;
    }
  }
  v_[i][cur_buf][cur_qua] = max_tmp;

  return best_next_qua;
}

double MPCAlgo::get_qvalue(size_t i, size_t cur_buf, size_t cur_qua,
                           size_t next_qua)
{
  double real_next_buf = real_buf_[cur_buf] - next_sending_time_[i][next_qua];
  return cur_ssim_[i][cur_qua]
         - lambda_ * fabs(cur_ssim_[i][cur_qua] - cur_ssim_[i+1][next_qua])
         - mu_ * max(0.0, -real_next_buf)
         + get_value(i+1, dis_buf(max(0.0, real_next_buf) + chunk_length_), next_qua);
}

double MPCAlgo::get_value(size_t i, size_t cur_buf, size_t cur_qua)
{
  if (flag_[i][cur_buf][cur_qua] != cnt) {
    update_value(i, cur_buf, cur_qua);
  }
  return v_[i][cur_buf][cur_qua];
}

size_t MPCAlgo::dis_buf(double buf) {
  size_t tmp = (buf + unit_buf_length_ * 0.5) / unit_buf_length_;
  return max((size_t)0, min(dis_buf_length_, tmp));
}
