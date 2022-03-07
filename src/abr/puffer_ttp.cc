#include "puffer_ttp.hh"
#include "ws_client.hh"

using namespace std;

PufferTTP::PufferTTP(const WebSocketClient & client,
                     const string & abr_name, const YAML::Node & abr_config)
  : Puffer(client, abr_name, abr_config)
{
  /* load neural networks */
  if (abr_config["model_dir"]) {
    fs::path model_dir = abr_config["model_dir"].as<string>();
    cerr << "model_dir = " << model_dir << endl;

    for (size_t i = 0; i < MAX_LOOKAHEAD_HORIZON; i++) {
      /* load PyTorch models */
      string model_path = model_dir / ("cpp-" + to_string(i) + ".pt");
      if (not fs::exists(model_path)) {
        throw runtime_error("Model " + model_path + " does not exist");
      }
      ttp_modules_[i] = torch::jit::load(model_path);

      /* load normalization weights */
      ifstream ifs(model_dir / ("cpp-meta-" + to_string(i) + ".json"));
      json j = json::parse(ifs);

      obs_mean_[i] = j.at("obs_mean").get<vector<double>>();
      obs_std_[i] = j.at("obs_std").get<vector<double>>();
    }

    if (abr_name == "puffer_ttp_mle") {
      is_mle_= true;
    }

    if (abr_name == "puffer_ttp_no_tcp_info") {
      ttp_input_dim_ = 17;
      no_tcp_info_ = true;
    }

    if (abr_config["blur_params"]) {
      mean_val_ = abr_config["blur_params"]["mean_val"].as<double>();
      std_val_ = abr_config["blur_params"]["std_val"].as<double>();
      kernel_size_ = abr_config["blur_params"]["kernel_size"].as<int>();

      /* In our current blur cases, kernel size has been fixed to the number
       * of bins (21), so the following runtime_error should never been
       * triggered, but keep the check for future compatibility */
      if (kernel_size_ < 0) {
        throw runtime_error("invalid kernel_size, we need a positive value");
      }

      if (kernel_size_ % 2 == 0) {
        throw runtime_error("kernel size is even, we want odd number to blur");
      }

      if (std_val_ <= 0) {
        throw runtime_error("invalid std params, should > 0");
      }

      cerr << "blur_params: " << mean_val_ << ", "
           << std_val_ << ", " << kernel_size_ << endl;
      gaussian_kernel_vals_.resize(kernel_size_);
      calculate_gaussian_values();
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

void PufferTTP::calculate_gaussian_values()
{
  double gaussian_coefficient = 1.0 / (std_val_ * sqrt(2.0 * M_PI));
  int right_pos = kernel_size_ >> 1;
  int left_pos = -right_pos;

  /*  In our current case, kernel_size=21, so right_pos=10, left_pos=-10
   *  To construct the kernel for bluring, we sample the points
   *  whose x-axis is [-10,10] */
  for (int x = left_pos; x <= right_pos; x++) {
    double exp_idx = -(x - mean_val_) * (x - mean_val_) /
                     (2.0 * std_val_ * std_val_);
    double gaussian_val = gaussian_coefficient * exp(exp_idx);
    gaussian_kernel_vals_[x - left_pos] = gaussian_val;
  }
}

/* For future flexibility, this function just blurs one row at a time */
void PufferTTP::blur_probability(int horizontal_index, int format_index)
{
  int right = kernel_size_ >> 1;
  int left = -right;

  double sum_prob = 0.0;
  size_t dim_num = dis_sending_time_ + 1;
  vector<double> orignial_prob(dim_num);
  for (size_t k = 0; k < dim_num; k++) {
    orignial_prob[k] = sending_time_prob_[horizontal_index][format_index][k];
  }

  for (size_t k = 0; k < dim_num; k++) {
    double blurred_val = 0.0;
    for (int j = left; j <= right; j++) {
      int gaussian_index = j + (kernel_size_ >> 1);
      int covolute_index = (k + j + dim_num) % dim_num;
      blurred_val += gaussian_kernel_vals_[gaussian_index] *
                     orignial_prob[covolute_index];
    }
    sending_time_prob_[horizontal_index][format_index][k] = blurred_val;
    sum_prob += blurred_val;
  }

  /* normalized to make the sum of prob as 1.0 */
  for (size_t k = 0; k < dim_num; k++) {
    sending_time_prob_[horizontal_index][format_index][k] /= sum_prob;
  }
}

void PufferTTP::reinit_sending_time()
{
  /* prepare the raw inputs for ttp */
  const auto curr_tcp_info = client_.tcp_info().value();
  vector<double> raw_input;

  size_t num_past_chunks = past_chunks_.size();

  if (num_past_chunks == 0) {
    for (size_t i = 0; i < max_num_past_chunks_; i++) {
      if (not no_tcp_info_) {
        raw_input.insert(raw_input.end(), {
          (double) curr_tcp_info.delivery_rate / PKT_BYTES,
          (double) curr_tcp_info.cwnd,
          (double) curr_tcp_info.in_flight,
          (double) curr_tcp_info.min_rtt / MILLION,
          (double) curr_tcp_info.rtt / MILLION,
        });
      }
      raw_input.insert(raw_input.end(), {0, 0});
    }
  } else {
    auto it = past_chunks_.begin();
    for (size_t i = 0; i < max_num_past_chunks_; i++) {
      if (not no_tcp_info_) {
        raw_input.insert(raw_input.end(), {
          (double) it->delivery_rate / PKT_BYTES,
          (double) it->cwnd,
          (double) it->in_flight,
          (double) it->min_rtt / MILLION,
          (double) it->rtt / MILLION,
        });
      }

      raw_input.insert(raw_input.end(), {
        (double) it->size / PKT_BYTES,
        (double) it->trans_time / THOUSAND,
      });

      if (i + num_past_chunks >= max_num_past_chunks_) {
        it++;
      }
    }
  }

  if (not no_tcp_info_) {
    raw_input.insert(raw_input.end(), {
      (double) curr_tcp_info.delivery_rate / PKT_BYTES,
      (double) curr_tcp_info.cwnd,
      (double) curr_tcp_info.in_flight,
      (double) curr_tcp_info.min_rtt / MILLION,
      (double) curr_tcp_info.rtt / MILLION,
    });
  }
  raw_input.insert(raw_input.end(), {0});

  assert(raw_input.size() == ttp_input_dim_);

  for (size_t i = 1; i <= lookahead_horizon_; i++) {
    /* prepare the inputs for each ahead timestamp and format */
    static double inputs[MAX_NUM_FORMATS * TTP_INPUT_DIM];

    for (size_t j = 0; j < num_formats_; j++) {
      raw_input[ttp_input_dim_ - 1] = (double) curr_sizes_[i][j] / PKT_BYTES;
      vector<double> norm_input {raw_input};

      normalize_in_place(i - 1, norm_input);
      for (size_t k = 0; k < ttp_input_dim_; k++) {
        inputs[j * ttp_input_dim_ + k] = norm_input[k];
      }
    }

    /* feed in the input batch and get the output batch */
    vector<torch::jit::IValue> torch_inputs;

    /* Here from_blob has reshaped the input of one chunk
     * in to [num_formats, DIM-IN-SIZE] */
    torch_inputs.push_back(torch::from_blob(inputs,
                           {(int) num_formats_, (int)ttp_input_dim_},
                           torch::kF64));

    at::Tensor output = torch::softmax(
        ttp_modules_[i - 1].forward(torch_inputs).toTensor(), 1);
    assert((size_t) output.sizes()[1] > dis_sending_time_);

    /* extract distribution from the output */
    bool is_all_ban = true;

    for (size_t j = 0; j < num_formats_; j++) {
      if (curr_sizes_[i][j] < 0) {
        is_ban_[i][j] = true;
        continue;
      }

      if (is_mle_) {
        is_all_ban = false;
        size_t max_k = dis_sending_time_;
        double max_value = 0;
        double good_prob = 0;
        for (size_t k = 0; k < dis_sending_time_; k++) {
          double tmp = output[j][k].item<double>();

          good_prob += tmp;
          if (max_k == dis_sending_time_ or tmp > max_value) {
            max_k = k;
            max_value = tmp;
          }
        }

        if (good_prob > max_value) {
          max_k = dis_sending_time_;
        }

        for (size_t k = 0; k <= dis_sending_time_; k++) {
          sending_time_prob_[i][j][k] = (k == max_k);
        }
        continue;
      }

      double good_prob = 0;

      for (size_t k = 0; k < dis_sending_time_; k++) {
        double tmp = output[j][k].item<double>();

        if (tmp < st_prob_eps_) {
          sending_time_prob_[i][j][k] = 0;
          continue;
        }

        sending_time_prob_[i][j][k] = tmp;
        good_prob += tmp;
      }

      sending_time_prob_[i][j][dis_sending_time_] = 1 - good_prob;

      if (good_prob < ban_prob_) {
        is_ban_[i][j] = true;
      } else {
        is_ban_[i][j] = false;
        is_all_ban = false;
      }
    }

    if (is_all_ban) {
      deal_all_ban(i);
    }
  }

  /* Blur sending_time_prob_ (in place) if kernel_size_ > 0 */
  if (kernel_size_ > 0) {
    for (size_t i = 1; i <= lookahead_horizon_; i++) {
      for (size_t j = 0; j < num_formats_; j++) {
        blur_probability(i, j);
      }
    }
  }
}
