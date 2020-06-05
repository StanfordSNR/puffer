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
/* Note: X_buf_chunks represents a number of chunks, but may be fractional (as in paper) */
double BolaBasic::objective(const Encoded & encoded, double client_buf_chunks, double chunk_duration_s) const
{
  // paper uses V rather than Vp for objective
  double V = params.Vp / chunk_duration_s;
  return (V * (encoded.utility + params.gp) - client_buf_chunks) / encoded.size;
}

BolaBasic::Encoded BolaBasic::choose_max_objective(const std::vector<Encoded> & encoded_formats,
		double client_buf_chunks, double chunk_duration_s) const
{
  const auto chosen =
    max_element(encoded_formats.begin(), encoded_formats.end(),
          [this, client_buf_chunks, chunk_duration_s](const Encoded& a, const Encoded& b) {
             return objective(a, client_buf_chunks, chunk_duration_s) <
                objective(b, client_buf_chunks, chunk_duration_s);
          }
         );

  return *chosen;
}

void BolaBasic::do_logging(const std::vector<Encoded> & encoded_formats,
		double chunk_duration_s, uint64_t vts, const string & channel_name) const
{
  /* Log objectives and decisions (for Fig 1/2) */
  fig_1(encoded_formats, chunk_duration_s, vts, channel_name);
  fig_2(encoded_formats, chunk_duration_s, vts, channel_name);
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
     });

  // TODO: Log every x video_ts?
  // do_logging(encoded_formats, chunk_duration_s, next_vts, channel->name());

  /* 2. Using parameters, calculate objective for each format.
  * Choose format with max objective. */
  return choose_max_objective(encoded_formats, client_buf_chunks, chunk_duration_s).vf;
}

void BolaBasic::fig_1(const vector<Encoded> & encoded_formats,
		double chunk_duration_s, uint64_t vts, const string & channel_name) const
{
  const string outfilename = "abr/test/" + channel_name + "/fig1_vts" + to_string(vts) + "_out.txt";

  // avoid overwriting
  // (TODO: this is useful for test but may not be what we want for the real thing - similar comment for throwing below)
  ifstream fig_exists{outfilename};
  if (fig_exists) {
    return;
  }

  ofstream fig_out{outfilename};
  if (not fig_out.is_open()) {
    throw runtime_error("couldn't open " + outfilename);
  }

  unsigned nbuf_samples = 3; // Objectives are linear
  for (const Encoded & encoded : encoded_formats) {
    for (double client_buf_s = 0; client_buf_s <= 25.0; client_buf_s += 25.0 / nbuf_samples) {
      /* Write format, size, utility for Fig 1 */
      fig_out << encoded.vf.to_string() << " "
          << encoded.size << " "
          << encoded.utility << " "
          << client_buf_s << " "
          << objective(encoded, client_buf_s / chunk_duration_s, chunk_duration_s)
          << "\n";
    }
  }
}

void BolaBasic::fig_2(const vector<Encoded> & encoded_formats,
											double chunk_duration_s, uint64_t vts, const string & channel_name) const
{
  const string outfilename = "abr/test/" + channel_name + "/fig2_vts" + to_string(vts) + "_out.txt";

  // avoid overwriting
  // (TODO: this is useful for test but may not be what we want for the real thing - similar comment for throwing below)
  ifstream fig_exists{outfilename};
  if (fig_exists) {
    return;
  }

  ofstream fig_out{outfilename};
  if (not fig_out.is_open()) {
    throw runtime_error("couldn't open " + outfilename);
  }

  unsigned nbuf_samples = 1000; // Approx stepwise
  for (double client_buf_s = 0; client_buf_s <= 25.0; client_buf_s += 25.0 / nbuf_samples) {
    const Encoded & chosen =
      choose_max_objective(encoded_formats, client_buf_s / chunk_duration_s, chunk_duration_s);

    /* Format string and utility already in Fig 1 legend, but still nice to have here */
    fig_out << client_buf_s << " "
        << chosen.vf.to_string() << " "
        << chosen.size << " "
        << chosen.utility << " "
        << "\n";
  }
}
