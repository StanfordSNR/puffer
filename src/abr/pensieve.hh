#ifndef PENSIEVE_HH
#define PENSIEVE_HH

// TODO: Clean up duplicate includes
#include "abr_algo.hh"
#include <stdio.h>
#include <thread>
#include <cstdint>
#include <cstdlib>
#include "ipc_socket.hh"
#include "json.hpp"
#include <endian.h>

#include <deque>

using json = nlohmann::json;

// TODO: Include appropriate constants for controlling Pensieve

/*
static const size_t MAX_NUM_PAST_CHUNKS = 5;
static const size_t MAX_LOOKAHEAD_HORIZON = 10;
static const double REBUFFER_LENGTH_COEFF = 5;
static const double SSIM_DIFF_COEFF = 0.3;
static const size_t MAX_NUM_FORMATS = 10;
*/

class Pensieve : public ABRAlgo
{
public:
  Pensieve(const WebSocketClient & client,
      const std::string & abr_name, const YAML::Node & abr_config);

  void video_chunk_acked(const VideoFormat & format,
                         const double ssim,
                         const unsigned int size,
                         const uint64_t trans_time) override;
  VideoFormat select_video_format() override;

private:
  /* past chunks and max number of them */
  /*
  size_t max_num_past_chunks_ {MAX_NUM_PAST_CHUNKS};
  std::deque<Chunk> past_chunks_ {};
  */

  /* all the time durations are measured in sec
  size_t max_lookahead_horizon_ {MAX_LOOKAHEAD_HORIZON};
  size_t lookahead_horizon_ {};
  double chunk_length_ {};
  size_t dis_buf_length_ {MAX_DIS_BUF_LENGTH};
  double unit_buf_length_ {};
  size_t num_formats_ {};
  double rebuffer_length_coeff_ {REBUFFER_LENGTH_COEFF};
  double ssim_diff_coeff_ {SSIM_DIFF_COEFF};
  */

  /* for the current buffer length and quality */
  size_t curr_buffer_ {};
  size_t curr_format_ {};

  size_t next_format_ {};
  static FileDescriptor open_ipc() {
    std::cout << "removing old ipc" << std::endl;
    remove("/tmp/pensieve");
    IPCSocket sock;
    sock.bind("/tmp/pensieve");
    // TODO: Configurable paths etc., use settings.yml
    sock.listen();
    // TODO: Close this using pclose() during client teardown
    auto result = popen("python2 /home/hudson/puffer/third_party/pensieve/multi_video_sim/rl_test.py /home/hudson/nn_model_ep_77400.ckpt", "r");
    std::cout << result << std::endl;
    std::cout << "waiting for connection" << std::endl;
    return sock.accept();
  };
  FileDescriptor connection_ {open_ipc()};

  /* the estimation of sending time given the timestamp and format */
  //double curr_sending_time_[MAX_LOOKAHEAD_HORIZON + 1][MAX_NUM_FORMATS] {};

  //void reinit();

};

#endif /* PENIEVE_HH */
