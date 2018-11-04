#ifndef ABR_ALGO_HH
#define ABR_ALGO_HH

#include "yaml-cpp/yaml.h"

class ABRAlgo
{
public:
  std::string abr_name() const { return abr_name_; }

protected:
  ABRAlgo(const std::string & abr_name) : abr_name_(abr_name) {}

private:
  std::string abr_name_;
};

#endif /* ABR_ALGO_HH */
