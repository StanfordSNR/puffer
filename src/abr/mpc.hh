#ifndef MPC_HH
#define MPC_HH

#include "abr_algo.hh"

#include <deque>

class MPC : public ABRAlgo
{
public:
  MPC(const WebSocketClient & client,
      const std::string & abr_name, const YAML::Node & abr_config);

  void video_chunk_acked(Chunk && c) override;
  VideoFormat select_video_format() override;

private:
  static constexpr size_t MAX_NUM_PAST_CHUNKS = 5;
  static constexpr size_t MAX_LOOKAHEAD_HORIZON = 10;
  static constexpr size_t MAX_DIS_BUF_LENGTH = 100;
  static constexpr double REBUFFER_LENGTH_COEFF = 20;
  static constexpr double SSIM_DIFF_COEFF = 1;
  static constexpr size_t MAX_NUM_FORMATS = 20;
  static constexpr double HIGH_SENDING_TIME = 10000;

  /* past chunks and max number of them */
  struct ChunkInfo {
    double ssim;          /* chunk ssim */
    unsigned int size;    /* chunk size */
    uint64_t trans_time;  /* transmission time */
    double pred_err;      /* throughput prediction error */
  };
  size_t max_num_past_chunks_ {MAX_NUM_PAST_CHUNKS};
  std::deque<ChunkInfo> past_chunks_ {};

  /* all the time durations are measured in sec */
  size_t max_lookahead_horizon_ {MAX_LOOKAHEAD_HORIZON};
  size_t lookahead_horizon_ {};
  double chunk_length_ {};
  size_t dis_buf_length_ {MAX_DIS_BUF_LENGTH};
  double unit_buf_length_ {};
  size_t num_formats_ {};
  double rebuffer_length_coeff_ {REBUFFER_LENGTH_COEFF};
  double ssim_diff_coeff_ {SSIM_DIFF_COEFF};

  /* for robust mpc */
  bool is_robust_ {false};
  double last_tp_pred_ {-1};

  /* whether the current chunk is the first chunk */
  bool is_init_ {};

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
