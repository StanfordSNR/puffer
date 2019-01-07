#ifndef PENSIEVE_HH
#define PENSIEVE_HH

#include "abr_algo.hh"
#include <stdio.h>
#include <thread>
#include <cstdint>
#include <cstdlib>
#include "ipc_socket.hh"
#include "child_process.hh"
#include "json.hpp"
#include <endian.h>

#include <deque>

using json = nlohmann::json;


class Pensieve : public ABRAlgo
{
public:
  Pensieve(const WebSocketClient & client,
      const std::string & abr_name, const YAML::Node & abr_config);
  ~Pensieve();

  void video_chunk_acked(const VideoFormat & format,
                         const double ssim,
                         const unsigned int size,
                         const uint64_t trans_time) override;
  VideoFormat select_video_format() override;

private:
  /* for the current buffer length and quality */
  size_t curr_buffer_ {};
  size_t curr_format_ {};

  size_t next_br_index_ {0};
  FileDescriptor connection_ {0};
  std::unique_ptr<ChildProcess> pensieve_proc_ { nullptr };

};

#endif /* PENIEVE_HH */
