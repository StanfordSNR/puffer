#ifndef LINEAR_BBA_HH
#define LINEAR_BBA_HH

#include "abr_algo.hh"

class LinearBBA : public ABRAlgo
{
public:
  LinearBBA(const WebSocketClient & client,
            const std::string & abr_name, const YAML::Node & abr_config);

  VideoFormat select_video_format() override;

private:
  static constexpr double LOWER_RESERVOIR = 0.2;
  static constexpr double UPPER_RESERVOIR = 0.8;

  double lower_reservoir_ {LOWER_RESERVOIR};
  double upper_reservoir_ {UPPER_RESERVOIR};
};

#endif /* LINEAR_BBA_HH */
