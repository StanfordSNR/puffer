#include "abr_algo.hh"
#include <cmath>

using namespace std;

double ssim_db(const double ssim)
{
  if (ssim != 1) {
    return -10 * log10(1 - ssim);
  } else {
    return INVALID_SSIM_DB;
  }
}
