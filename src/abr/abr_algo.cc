#include "abr_algo.hh"
#include <cmath>

using namespace std;

double ssim_db(const double ssim)
{
  if (ssim != 1) {
    return max(MIN_SSIM, min(MAX_SSIM, -10 * log10(1 - ssim)));
  } else {
    return MAX_SSIM;
  }
}
