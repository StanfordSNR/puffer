#include "mpc.hh"
#include "ws_client.hh"

using namespace std;

MPC::MPC(const WebSocketClient & client,
         const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name), past_chunks_()
{
  if (abr_config["past_chunk_cnt"]) {
    past_chunk_cnt_ = abr_config["past_chunk_cnt"].as<size_t>();
  }
}

void MPC::reset()
{
  past_chunks_.clear();
}

void MPC::video_chunk_acked(const VideoFormat & format,
                            const double ssim,
                            const unsigned int chunk_size,
                            const uint64_t trans_time)
{
  past_chunks_.emplace_back(Chunk{format, ssim, chunk_size, trans_time});
  if (past_chunks_.size() > past_chunk_cnt_) {
    past_chunks_.pop_front();
  }
}

VideoFormat MPC::select_video_format()
{
  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  // TODO
  return vformats[0];
}
