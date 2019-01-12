#include "puffer_ttp.hh"
#include "ws_client.hh"
#include "torch/script.h"

using namespace std;

PufferTTP::PufferTTP(const WebSocketClient & client,
                     const string & abr_name, const YAML::Node & abr_config)
  : Puffer(client, abr_name, abr_config)
{
  /* load neural networks */
  if (abr_config["model_dir"]) {
    fs::path model_dir = abr_config["model_dir"].as<string>();

    for (size_t i = 0; i < MAX_LOOKAHEAD_HORIZON; i++) {
      // load PyTorch models
      string model_path = model_dir / ("cpp-" + to_string(i) + ".pt");
      ttp_modules_[i] = torch::jit::load(model_path.c_str());
      if (not ttp_modules_[i]) {
        throw runtime_error("Model " + model_path + " does not exist");
      }

      // load normalization weights
      ifstream ifs(model_dir / ("cpp-meta-" + to_string(i) + ".json"));
      json j = json::parse(ifs);

      obs_mean_[i] = j.at("obs_mean").get<vector<double>>();
      obs_std_[i] = j.at("obs_std").get<vector<double>>();
    }
  } else {
    throw runtime_error("Puffer requires specifying model_dir in abr_config");
  }
}

void PufferTTP::normalize_in_place(size_t i, vector<double> & input)
{
  assert(input.size() == obs_mean_[i].size());
  assert(input.size() == obs_std_[i].size());

  for (size_t j = 0; j < input.size(); j++) {
    input[j] -= obs_mean_[i][j];

    if (obs_std_[i][j] != 0) {
      input[j] /= obs_std_[i][j];
    }
  }
}
