#include "bola_basic.hh"
#include "ws_client.hh"
#include <math.h>
#include <fstream>
#include <algorithm>

using namespace std;

BolaBasic::BolaBasic(const WebSocketClient & client, const string & abr_name)
  : ABRAlgo(client, abr_name)
{
  if (abr_name_ == "bola_basic_v1") {
    version = BOLA_BASIC_v1;
    params = PARAMS_V1;
  } else {
    version = BOLA_BASIC_v2;
    params = PARAMS_V2;
  }
}

/* Size/buf units don't affect gp (if consistent). Utility units do. */
BolaBasic::Parameters
BolaBasic::calculate_params(BolaBasic::Version version) const
{
  /* vf is not meaningful, since these are averages over past encodings across
   * channels */
  const VideoFormat fake = VideoFormat("11x11-11");
  const Encoded smallest = { fake,
    size_ladder_bytes.front(), utility(ssim_index_ladder.front(), version) };
  const Encoded second_smallest = { fake,
    size_ladder_bytes.at(1), utility(ssim_index_ladder.at(1), version) };
  const Encoded largest = { fake,
    size_ladder_bytes.back(), utility(ssim_index_ladder.back(), version) };

  const size_t size_delta = second_smallest.size - smallest.size;

  /* BOLA version determines high utility used in calculation:
   * BOLA_BASIC_v1: Use best utility in static ladder.
   * BOLA_BASIC_v2: Use max possible utility. */
  const double utility_high = version == BOLA_BASIC_v1 ? largest.utility : utility(1, version);

  const double gp = (
    MAX_BUF_S * ( second_smallest.size * smallest.utility -
                  smallest.size * second_smallest.utility ) -
    utility_high * MIN_BUF_S * size_delta
  ) / (
    (MIN_BUF_S - MAX_BUF_S) * size_delta
  );

  const double Vp = MAX_BUF_S / (utility_high + gp);

  return {Vp, gp};
}

/* Note BOLA uses the raw value of utility directly. */
double BolaBasic::utility(double raw_ssim, BolaBasic::Version version)
{
  return version == BOLA_BASIC_v1 ? ssim_db(raw_ssim) : raw_ssim;
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

BolaBasic::Encoded
BolaBasic::choose_max_scaled_utility(const vector<Encoded> & encoded_formats) const
{
  const auto chosen = max_element(encoded_formats.begin(),
                                  encoded_formats.end(),
    [this](const Encoded& a, const Encoded& b) {
      return a.utility + params.gp < b.utility + params.gp;
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
      return Encoded { vf, get<1>(data_map.at(vf)), utility(ssim_map.at(vf), version) };
    }
  );

  /* 2. Using parameters, calculate objective for each format.
  * BOLA_BASIC_v1: Choose format with max objective.
  * BOLA_BASIC_v2:
  *   If max objective is nonnegative, choose format with max objective.
  *   Else, choose format with max (utility + gp). */
  const Encoded max_obj_format =
    choose_max_objective(encoded_formats, client_buf_chunks, chunk_duration_s);
  const double max_obj_value =
    objective(max_obj_format, client_buf_chunks, chunk_duration_s);

  if (version == BOLA_BASIC_v1 or max_obj_value >= 0) {
    return max_obj_format.vf;
  } else {
    return choose_max_scaled_utility(encoded_formats).vf;
  }
}
