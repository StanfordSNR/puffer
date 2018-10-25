#include "ws_client.hh"
#include "channel.hh"
#include "abr_algo.hh"

using namespace std;

static const size_t MAX_DIS_BUF_LENGTH = 100;
static const size_t MAX_NUM_FORMATS = 20;
static const size_t MAX_PRE_HORIZON = 10;
static const size_t MAX_FRONT_HORIZON = 10;
static const double LAMBDA = 0.3;
static const double MU = 5;

class MPCAlgo : public ABRAlgo
{
private:
  /* all the time durations are measured in sec */
  size_t max_front_horizon_ {};
  size_t front_horizon_ {};
  double chunk_length_ {};
  size_t dis_buf_length_ {};
  double unit_buf_length_ {};
  size_t pre_horizon_ {};
  size_t num_formats_ {};
  double lambda_ {};
  double mu_ {};

  /* for the current buffer length and quality */
  size_t cur_buf_ {};
  size_t cur_qua_ {};

  /* for storing the value fucntion*/
  uint64_t flag_[MAX_FRONT_HORIZON + 1][MAX_DIS_BUF_LENGTH + 1][MAX_NUM_FORMATS] {};
  double v_[MAX_FRONT_HORIZON + 1][MAX_DIS_BUF_LENGTH + 1][MAX_NUM_FORMATS] {};
  uint64_t cnt {};

  /* map the discretized buffer length to the estimation */
  double real_buf_[MAX_DIS_BUF_LENGTH + 1];

  /* unit sending time estimation */
  double unit_sending_time_[MAX_FRONT_HORIZON + 1 + MAX_PRE_HORIZON];

  /* the ssim of the chunk given the timestamp and format */
  double cur_ssim_[MAX_FRONT_HORIZON + 1][MAX_NUM_FORMATS];

  /* the estimation of sending time given the timestamp and format */
  double next_sending_time_[MAX_FRONT_HORIZON + 1][MAX_NUM_FORMATS];

  void print_info();

public:
  MPCAlgo(const double max_buf_length,
          const size_t max_front_horizon = MAX_FRONT_HORIZON,
          const size_t dis_buf_length = MAX_DIS_BUF_LENGTH,
          const double lambda = LAMBDA,
          const double mu = MU);

  bool is_ready(const WebSocketClient & client, const Channel & channel) override;

  const VideoFormat & select_video_quality(const WebSocketClient & client,
                                           const Channel & channel) override;

  void reinit(const WebSocketClient & client, const Channel & channel);

  /* calculate the value of curresponding state and return the best strategy */
  size_t update_value(size_t i, size_t cur_buf, size_t cur_qua);

  /* return the qvalue of the given cur state and next action */
  double get_qvalue(size_t i, size_t cur_buf, size_t cur_qua, size_t next_qua);

  /* return the value of the given state */
  double get_value(size_t i, size_t cur_buf, size_t cur_qua);

  /* discretize the buffer length */
  size_t dis_buf(double buf);
};
