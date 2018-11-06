#ifndef LINEAR_BBA_HH
#define LINEAR_BBA_HH

#include "abr_algo.hh"

static const double LOWER_RESERVOIR = 0.2;
static const double UPPER_RESERVOIR = 0.8;

class LinearBBA : public ABRAlgo
{
public:
  LinearBBA(const WebSocketClient & client,
            const std::string & abr_name, const YAML::Node & abr_config);

  VideoFormat select_video_format() override;

private:
  double lower_reservoir_ {LOWER_RESERVOIR};
  double upper_reservoir_ {UPPER_RESERVOIR};
};

#endif /* LINEAR_BBA_HH */
