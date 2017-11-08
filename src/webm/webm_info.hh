#ifndef WEBM_INFO_HH
#define WEBM_INFO_HH

namespace WebM
{
class WebmInfo
{
public:
  std::tuple<uint32_t, uint32_t> get_timescale_duration();
  uint32_t get_bitrate();
  uint32_t get_bitrate(uint32_t timescale, uint32_t duration);
  uint32_t get_sample_rate();
}
}

#endif /* WEBM_INFO_HH */
