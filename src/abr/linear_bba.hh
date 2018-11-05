#ifndef LINEAR_BBA_HH
#define LINEAR_BBA_HH

#include "abr_algo.hh"

class LinearBBA : public ABRAlgo
{
public:
  LinearBBA(const WebSocketClient & client,
            const std::string & abr_name, const YAML::Node & abr_config);

  void reset() {}
  VideoFormat select_video_format();

private:
  double lower_reservoir_ {0.2};
  double upper_reservoir_ {0.8};
};

#endif /* LINEAR_BBA_HH */
