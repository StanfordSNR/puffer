#ifndef MPC_HH
#define MPC_HH

#include "abr_algo.hh"

#include <deque>

static const size_t MAX_NUM_PAST_CHUNKS = 5;
static const size_t MAX_LOOKAHEAD_HORIZON = 10;
static const size_t MAX_DIS_BUF_LENGTH = 100;
static const double REBUFFER_LENGTH_COEFF = 5;
static const double SSIM_DIFF_COEFF = 0.3;
static const size_t MAX_NUM_FORMATS = 20;

class MPC : public ABRAlgo
{
public:
  MPC(const WebSocketClient & client,
      const std::string & abr_name, const YAML::Node & abr_config);

  void video_chunk_acked(const VideoFormat & format,
                         const double ssim,
                         const unsigned int size,
                         const uint64_t trans_time) override;
  VideoFormat select_video_format() override;

private:
  /* past chunks and max number of them */
  size_t max_num_past_chunks_ {MAX_NUM_PAST_CHUNKS};
  std::deque<Chunk> past_chunks_ {};

  /* all the time durations are measured in sec */
  size_t max_lookahead_horizon_ {MAX_LOOKAHEAD_HORIZON};
  size_t lookahead_horizon_ {};
  double chunk_length_ {};
  size_t dis_buf_length_ {MAX_DIS_BUF_LENGTH};
  double unit_buf_length_ {};
  size_t num_formats_ {};
  double rebuffer_length_coeff_ {REBUFFER_LENGTH_COEFF};
  double ssim_diff_coeff_ {SSIM_DIFF_COEFF};

  /* for the current buffer length */
  size_t curr_buffer_ {};

  /* for storing the value function */
  uint64_t flag_[MAX_LOOKAHEAD_HORIZON + 1][MAX_DIS_BUF_LENGTH + 1][MAX_NUM_FORMATS] {};
  double v_[MAX_LOOKAHEAD_HORIZON + 1][MAX_DIS_BUF_LENGTH + 1][MAX_NUM_FORMATS] {};

  /* record the current round of DP */
  uint64_t curr_round_ {};

  /* map the discretized buffer length to the estimation */
  double real_buffer_[MAX_DIS_BUF_LENGTH + 1] {};

  /* unit sending time estimation */
  double unit_sending_time_[MAX_LOOKAHEAD_HORIZON + 1 + MAX_NUM_PAST_CHUNKS] {};

  /* the ssim of the chunk given the timestamp and format */
  double curr_ssims_[MAX_LOOKAHEAD_HORIZON + 1][MAX_NUM_FORMATS] {};

  /* the estimation of sending time given the timestamp and format */
  double curr_sending_time_[MAX_LOOKAHEAD_HORIZON + 1][MAX_NUM_FORMATS] {};

  void reinit();

  /* calculate the value of corresponding state and return the best strategy */
  size_t update_value(size_t i, size_t curr_buffer, size_t curr_format);

  /* return the qvalue of the given cur state and next action */
  double get_qvalue(size_t i, size_t curr_buffer, size_t curr_format,
                    size_t next_format);

  /* return the value of the given state */
  double get_value(size_t i, size_t curr_buffer, size_t curr_format);

  /* discretize the buffer length */
  size_t discretize_buffer(double buf);
};

#endif /* MPC_HH */
