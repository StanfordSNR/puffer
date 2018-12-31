#include "linear_bba.hh"
#include "ws_client.hh"

using namespace std;

LinearBBA::LinearBBA(const WebSocketClient & client,
                     const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{
  if (abr_config["lower_reservoir"]) {
    lower_reservoir_ = abr_config["lower_reservoir"].as<double>();
  }

  if (abr_config["upper_reservoir"]) {
    upper_reservoir_ = abr_config["upper_reservoir"].as<double>();
  }
}

VideoFormat LinearBBA::select_video_format()
{
  double max_buffer_s = WebSocketClient::MAX_BUFFER_S;
  double buf = min(max(client_.video_playback_buf(), 0.0), max_buffer_s);

  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  size_t vformats_cnt = vformats.size();

  uint64_t next_vts = client_.next_vts().value();
  const auto & data_map = channel->vdata(next_vts);
  const auto & ssim_map = channel->vssim(next_vts);

  /* get max and min chunk size for the next video ts */
  size_t max_idx = vformats_cnt, max_size = 0;
  size_t min_idx = vformats_cnt, min_size = SIZE_MAX;

  for (size_t i = 0; i < vformats_cnt; i++) {
    const auto & vf = vformats[i];
    size_t chunk_size = get<1>(data_map.at(vf));

    if (chunk_size > max_size) {
      max_size = chunk_size;
      max_idx = i;
    }

    if (chunk_size < min_size) {
      min_size = chunk_size;
      min_idx = i;
    }
  }

  assert(max_idx < vformats_cnt);
  assert(min_idx < vformats_cnt);

  /* lower and uppper reservoirs */
  if (buf >= upper_reservoir_ * max_buffer_s) {
    return vformats[max_idx];
  } else if (buf <= lower_reservoir_ * max_buffer_s) {
    return vformats[min_idx];
  }

  /* pick the chunk with highest SSIM but with size <= max_serve_size */
  double slope = (max_size - min_size) /
                 ((upper_reservoir_ - lower_reservoir_) * max_buffer_s);
  double max_serve_size = min_size +
                          slope * (buf - lower_reservoir_ * max_buffer_s);

  double highest_ssim = -2;
  size_t ret_idx = vformats_cnt;

  for (size_t i = 0; i < vformats_cnt; i++) {
    const auto & vf = vformats[i];
    size_t chunk_size = get<1>(data_map.at(vf));
    if (chunk_size > max_serve_size) {
      continue;
    }

    double ssim = ssim_map.at(vf);
    if (ssim > highest_ssim) {
      highest_ssim = ssim;
      ret_idx = i;
    }
  }

  assert(ret_idx < vformats_cnt);
  return vformats[ret_idx];
}
