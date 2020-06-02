#ifndef BOLA_BASIC_HH
#define BOLA_BASIC_HH

#include <vector>
#include <array>
#include <fstream>
#include <string>
#include "abr_algo.hh"
#include "ws_client.hh"

/*
 * BOLA-BASIC, with V and gamma set statically based on min/max buffer level:
 * Min buffer: Intersection of objectives for smallest and next-smallest formats
 *    i.e. set objectives equal, using Q = min buf
 *    This is an extrapolation from the paper.
 * Max buffer: If buffer > max buf, don't send (enforced by media server)
 *    i.e. V(utility_best + gp) = max buf
 *    This is directly from the paper.
 */
class BolaBasic : public ABRAlgo
{
    public:
        BolaBasic(const WebSocketClient & client,
                  const std::string & abr_name, const YAML::Node & abr_config);

        VideoFormat select_video_format() override;

    private:
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

        /* Static size/SSIM ladders (averages over past data), used to calculate parameters. */
        static constexpr unsigned NFORMATS = 10;

        static constexpr std::array<size_t, NFORMATS> size_ladder_bytes =
            { 44319, 93355, 115601, 142904, 196884, 263965, 353752, 494902, 632193, 889893 };

        static constexpr std::array<double, NFORMATS> ssim_index_ladder =
            { 0.91050748,  0.94062527,  0.94806355,  0.95498943,  0.96214503,
            0.96717277,  0.97273958,  0.97689813,  0.98004106,  0.98332605 };

        /* Ladders must be ordered nondecreasing.
         * Smallest size must be strictly less than next-smallest. */
        static_assert(size_ladder_bytes.front() < size_ladder_bytes.at(1));
        static_assert(ssim_index_ladder.front() <= ssim_index_ladder.at(1));

        /* Min/max buffer level, in seconds. */
        static constexpr double MIN_BUF_S = 3;
        static constexpr double MAX_BUF_S = WebSocketClient::MAX_BUFFER_S;
        /* Min buf must be strictly less than max */
        static_assert(MIN_BUF_S < MAX_BUF_S);

        // vf is not meaningful, since these are averages over past encodings across channels
        const VideoFormat fake = VideoFormat("11x11-11");
        const Encoded smallest = { fake,
            size_ladder_bytes.front(), utility(ssim_index_ladder.front()) };
        const Encoded second_smallest = { fake,
            size_ladder_bytes.at(1), utility(ssim_index_ladder.at(1)) };
        const Encoded largest = { fake,
            size_ladder_bytes.back(), utility(ssim_index_ladder.back()) };

        const size_t size_delta = second_smallest.size - smallest.size;

        /* Size/buf units don't affect gp (if consistent). Utility units do. */
        const double gp = (
             MAX_BUF_S * ( second_smallest.size * smallest.utility - smallest.size * second_smallest.utility ) -
                /* Best utility = largest (assumes utility is nondecreasing with size, as required by BOLA.) */
                largest.utility * MIN_BUF_S * size_delta
         ) / (
            (MIN_BUF_S - MAX_BUF_S) * size_delta
         );

        const double Vp = MAX_BUF_S / (largest.utility + gp);

        const Parameters params = {Vp, gp};

        /* A format's utility to the client. */
        double utility(double raw_ssim) const;

        /* Definition of objective from paper. */
        double objective(const Encoded& encoded, double client_buf_chunks, double chunk_duration_s) const;

        /* Return format with maximum objective. */
        Encoded choose_max_objective(const std::vector<Encoded> & encoded_formats,
                                     double client_buf_chunks, double chunk_duration_s) const;

        /* Log objectives and decisions */
        void do_logging(const std::vector<Encoded> & encoded_formats,
                        double chunk_duration_s, uint64_t vts, const std::string & channel_name) const;

        /* Output the data for paper's Fig 1 (objectives).
         * Plotting script can scale objective to match units in paper (size in bits) */
        void fig_1(const std::vector<Encoded> & encoded_formats,
                   double chunk_duration_s, uint64_t vts, const std::string & channel_name) const;

        /* Output the data for paper's Fig 2 (decisions). */
        void fig_2(const std::vector<Encoded> & encoded_formats,
                   double chunk_duration_s, uint64_t vts, const std::string & channel_name) const;
};

#endif /* BOLA_BASIC_HH */
