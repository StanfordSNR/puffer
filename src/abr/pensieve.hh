#ifndef PENSIEVE_HH
#define PENSIEVE_HH

#include "abr_algo.hh"
#include "child_process.hh"
#include "filesystem.hh"

class Pensieve : public ABRAlgo
{
public:
  Pensieve(const WebSocketClient & client,
           const std::string & abr_name, const YAML::Node & abr_config);
  ~Pensieve();

  void video_chunk_acked(Chunk && c) override;
  VideoFormat select_video_format() override;

private:
  size_t next_br_index_ {};
  fs::path ipc_file_ {};
  std::unique_ptr<FileDescriptor> connection_ {nullptr};
  std::unique_ptr<ChildProcess> pensieve_proc_ {nullptr};
};

#endif /* PENSIEVE_HH */
