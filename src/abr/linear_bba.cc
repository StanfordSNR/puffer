#include "linear_bba.hh"

using namespace std;

LinearBBA::LinearBBA(const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(abr_name)
{
  if (abr_config["lower_reservoir"]) {
    lower_reservoir_ = abr_config["lower_reservoir"].as<double>();
  }

  if (abr_config["upper_reservoir"]) {
    upper_reservoir_ = abr_config["upper_reservoir"].as<double>();
  }
}
