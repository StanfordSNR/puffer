#ifndef MPC_HH
#define MPC_HH

#include "abr_algo.hh"

#include <deque>

class MPC : public ABRAlgo
{
public:
  MPC(const WebSocketClient & client,
      const std::string & abr_name, const YAML::Node & abr_config);

  void reset() override;
  void video_chunk_acked(const VideoFormat & format,
                         const double ssim,
                         const unsigned int chunk_size,
                         const uint64_t transmission_time) override;
  VideoFormat select_video_format() override;

private:
  size_t past_chunk_cnt_ {5};

  std::deque<Chunk> past_chunks_;
};

#endif /* MPC_HH */
