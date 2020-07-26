#ifndef BOLA_BASIC_HH
#define BOLA_BASIC_HH

#include <vector>
#include <array>
#include <fstream>
#include <string>
#include "abr_algo.hh"
#include "ws_client.hh"

/*
* BOLA-BASIC, with V and gamma set statically based on min/max buffer level,
* as in v3 of the paper:
*
* Min buffer: Intersection of objectives for smallest and next-smallest formats
*    i.e. set objectives equal, using Q = min buf
* Max buffer: If buffer > max buf, don't send (enforced by media server)
*    i.e. V(utility_high + gp) = max buf,
*    where utility_high is the maximum in the static ladder for BOLA_BASIC_v1,
*    or the maximum possible utility for BOLA_BASIC_v2.
*/
class BolaBasic : public ABRAlgo
{
public:
  BolaBasic(const WebSocketClient & client,
            const std::string & abr_name);

  VideoFormat select_video_format() override;

private:
  /* Version of BOLA-BASIC */
  enum Version {
    // Original
    BOLA_BASIC_v1,
    // With changes suggested in kspiteri's 2020-07-18 email
    BOLA_BASIC_v2,
  };

  Version version = BOLA_BASIC_v1;

  /* BOLA parameters V and gamma, multiplied by p */
  struct Parameters {
    double Vp;
    double gp;
  };

  /* Represents an encoded format */
  struct Encoded {
    VideoFormat vf;
    size_t size;    // bytes (as in vdata map)
    double utility;
  };

  /* Static size/SSIM ladders (averages over past data), used to calculate
   * parameters. */
  static constexpr unsigned NFORMATS = 10;

  static constexpr std::array<size_t, NFORMATS> size_ladder_bytes =
    { 44319, 93355, 115601, 142904, 196884, 263965, 353752, 494902, 632193,
      889893 };

  static constexpr std::array<double, NFORMATS> ssim_index_ladder =
    { 0.91050748, 0.94062527, 0.94806355, 0.95498943, 0.96214503,
      0.96717277, 0.97273958, 0.97689813, 0.98004106, 0.98332605 };

  /* Ladders must be ordered nondecreasing.
  * Smallest size must be strictly less than next-smallest. */
  static_assert(size_ladder_bytes.front() < size_ladder_bytes.at(1));
  static_assert(ssim_index_ladder.front() <= ssim_index_ladder.at(1));

  /* Min/max buffer level, in seconds. */
  static constexpr double MIN_BUF_S = 3;
  static constexpr double MAX_BUF_S = WebSocketClient::MAX_BUFFER_S;
  /* Min buf must be strictly less than max */
  static_assert(MIN_BUF_S < MAX_BUF_S);

  /* Calculate parameters appropriate for BOLA version. */
  Parameters calculate_params(Version version) const;

  /* Calculate both versions of parameters statically,
   * then choose dynamically based on config. */
  const Parameters PARAMS_V1 = calculate_params(BOLA_BASIC_v1);
  const Parameters PARAMS_V2 = calculate_params(BOLA_BASIC_v2);
  Parameters params = PARAMS_V1;

  /* A format's utility to the client.
   * Takes version as arg, to allow use before configured version is known. */
  static double utility(double raw_ssim, Version version);

  /* Definition of objective from paper. */
  double objective(const Encoded & encoded, double client_buf_chunks,
                   double chunk_duration_s) const;

  /* Return format with maximum objective. */
  Encoded choose_max_objective(const std::vector<Encoded> & encoded_formats,
                               double client_buf_chunks,
                               double chunk_duration_s) const;

  /* Return format with maximum (utility + gp). */
  Encoded choose_max_scaled_utility(const std::vector<Encoded> & encoded_formats) const;
};

#endif /* BOLA_BASIC_HH */
