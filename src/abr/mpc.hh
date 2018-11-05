#ifndef MPC_HH
#define MPC_HH

#include "abr_algo.hh"

#include <deque>

static const size_t MAX_DIS_BUF_LENGTH = 100;
static const size_t MAX_NUM_FORMATS = 20;
static const size_t PAST_CHUNK_CNT = 5;
static const size_t MAX_LOOKAHEAD_HORIZON = 10;
static const double REBUFFER_LENGTH_COEFF = 5;
static const double SSIM_DIFF_COEFF = 0.3;

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
  size_t past_chunk_cnt_ {PAST_CHUNK_CNT};

  std::deque<Chunk> past_chunks_ {};

  /* all the time durations are measured in sec */
  size_t max_lookahead_horizon_ {};
  size_t lookahead_horizon_ {};
  double chunk_length_ {};
  size_t dis_buf_length_ {};
  double unit_buf_length_ {};
  size_t num_formats_ {};
  double rebuffer_length_coeff_ {};
  double ssim_diff_coeff_ {};

  /* for the current buffer length and quality */
  size_t curr_buffer_ {};
  size_t curr_format_ {};

  /* for storing the value fucntion*/
  uint64_t flag_[MAX_LOOKAHEAD_HORIZON + 1][MAX_DIS_BUF_LENGTH + 1][MAX_NUM_FORMATS] {};
  double v_[MAX_LOOKAHEAD_HORIZON + 1][MAX_DIS_BUF_LENGTH + 1][MAX_NUM_FORMATS] {};

  /* record the current dp round */
  uint64_t curr_round_ {};

  /* map the discretized buffer length to the estimation */
  double real_buffer_[MAX_DIS_BUF_LENGTH + 1] {};

  /* unit sending time estimation */
  double unit_sending_time_[MAX_LOOKAHEAD_HORIZON + 1 + PAST_CHUNK_CNT] {};

  /* the ssim of the chunk given the timestamp and format */
  double curr_ssims_[MAX_LOOKAHEAD_HORIZON + 1][MAX_NUM_FORMATS] {};

  /* the estimation of sending time given the timestamp and format */
  double curr_sending_time_[MAX_LOOKAHEAD_HORIZON + 1][MAX_NUM_FORMATS] {};

  void reinit();

  /* calculate the value of curresponding state and return the best strategy */
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
