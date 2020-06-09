#include "bola_basic.hh"
#include "ws_client.hh"
#include <math.h>
#include <fstream>
#include <algorithm>

using namespace std;

BolaBasic::BolaBasic(const WebSocketClient & client, const string & abr_name)
  : ABRAlgo(client, abr_name)
{
  // TODO: Make gamma and V configurable
}

/* Note BOLA uses the raw value of utility directly. */
double BolaBasic::utility(double raw_ssim) const
{
  return ssim_db(raw_ssim);
}

/* Size units affect objective value, but not the decision */
/* Note: X_buf_chunks represents a number of chunks,
 * but may be fractional (as in paper) */
double BolaBasic::objective(const Encoded & encoded, double client_buf_chunks,
                            double chunk_duration_s) const
{
  // paper uses V rather than Vp for objective
  double V = params.Vp / chunk_duration_s;
  return (V * (encoded.utility + params.gp) - client_buf_chunks) / encoded.size;
}

BolaBasic::Encoded
BolaBasic::choose_max_objective(const vector<Encoded> & encoded_formats,
                                double client_buf_chunks,
                                double chunk_duration_s) const
{
  const auto chosen = max_element(encoded_formats.begin(),
                                  encoded_formats.end(),
    [this, client_buf_chunks, chunk_duration_s](const Encoded& a,
                                                const Encoded& b) {
      return objective(a, client_buf_chunks, chunk_duration_s) <
             objective(b, client_buf_chunks, chunk_duration_s);
    }
  );

  return *chosen;
}

VideoFormat BolaBasic::select_video_format()
{
  const auto & channel = client_.channel();
  double chunk_duration_s = channel->vduration() * 1.0 / channel->timescale();
  double client_buf_s = max(client_.video_playback_buf(), 0.0);
  double client_buf_chunks = client_buf_s / chunk_duration_s;

  /* 1. Get info for each encoded format */
  vector<Encoded> encoded_formats;
  uint64_t next_vts = client_.next_vts().value();
  const auto & data_map = channel->vdata(next_vts);
  const auto & ssim_map = channel->vssim(next_vts);
  const auto & vformats = channel->vformats();

  transform(vformats.begin(), vformats.end(), back_inserter(encoded_formats),
    [this, data_map, ssim_map](const VideoFormat & vf) {
      return Encoded { vf, get<1>(data_map.at(vf)), utility(ssim_map.at(vf)) };
    }
  );

  /* 2. Using parameters, calculate objective for each format.
  * Choose format with max objective. */
  return choose_max_objective(encoded_formats, client_buf_chunks,
                              chunk_duration_s).vf;
}
