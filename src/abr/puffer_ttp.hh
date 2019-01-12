#ifndef PUFFERTTP_HH
#define PUFFERTTP_HH

#include "puffer.hh"
#include "torch/script.h"

#include <deque>

class PufferTTP : public Puffer
{
public:
  PufferTTP(const WebSocketClient & client,
            const std::string & abr_name, const YAML::Node & abr_config);
private:
  std::shared_ptr<torch::jit::script::Module> ttp_modules_[MAX_LOOKAHEAD_HORIZON];

  /* stats of training data used for normalization */
  std::vector<double> obs_mean_[MAX_LOOKAHEAD_HORIZON];
  std::vector<double> obs_std_[MAX_LOOKAHEAD_HORIZON];

  /* preprocess the data */
  void normalize_in_place(size_t i, std::vector<double> & input);
};

#endif /* PUFFERTTP_HH */
