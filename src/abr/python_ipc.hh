#ifndef PYTHON_IPC_HH
#define PYTHON_IPC_HH

#include "abr_algo.hh"
#include "child_process.hh"
#include "filesystem.hh"
#include <map>

static const double DEFAULT_SSIM = 0.85; // unitless, about 8 SSIM dB
static const double DEFAULT_CHUNK_SIZE = 3.0; // Mb
static const size_t MAX_LOOKAHEAD_HORIZON = 5;
static const size_t ACTION_SPACE_N = 10;

class PythonIPC : public ABRAlgo
{
public:
  PythonIPC(const WebSocketClient & client,
           const std::string & abr_name, const YAML::Node & abr_config);
  ~PythonIPC();

  void video_chunk_acked(Chunk && c) override;
  VideoFormat select_video_format() override;

private:
  std::map<std::string, double> past_chunk_info_ {};
  fs::path ipc_file_ {};
  std::unique_ptr<FileDescriptor> connection_ {nullptr};
  std::unique_ptr<ChildProcess> python_proc_ {nullptr};
};

#endif /* PYTHON_IPC_HH */
