#ifndef PUFFER_TTP_HH
#define PUFFER_TTP_HH

#include "puffer.hh"
#include "torch/script.h"

#include <deque>

class PufferTTP : public Puffer
{
public:
  PufferTTP(const WebSocketClient & client,
            const std::string & abr_name, const YAML::Node & abr_config);
private:
  static constexpr double BAN_PROB_ = 0.5;
  static constexpr size_t TTP_INPUT_DIM = 62;
  static constexpr size_t TTP_CURR_DIFF_POS = 5;

  double ban_prob_ {BAN_PROB_};

  std::shared_ptr<torch::jit::script::Module> ttp_modules_[MAX_LOOKAHEAD_HORIZON];

  /* stats of training data used for normalization */
  std::vector<double> obs_mean_[MAX_LOOKAHEAD_HORIZON];
  std::vector<double> obs_std_[MAX_LOOKAHEAD_HORIZON];

  /* preprocess the data */
  void normalize_in_place(size_t i, std::vector<double> & input);

  void reinit_sending_time() override;
};

#endif /* PUFFER_TTP_HH */
