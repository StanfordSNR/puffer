#ifndef MP4_INFO_HH
#define MP4_INFO_HH

#include "mp4_parser.hh"

namespace MP4 {

class MP4Info
{
public:
  MP4Info(const std::shared_ptr<MP4Parser> & parser);

  std::tuple<uint32_t, uint64_t> get_timescale_duration();
  std::tuple<uint16_t, uint16_t> get_width_height();
  std::tuple<uint8_t, uint8_t> get_avc_profile_level();

private:
  std::shared_ptr<MP4Parser> parser_;
};

}

#endif /* MP4_INFO_HH */
