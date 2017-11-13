#ifndef MP4_INFO_HH
#define MP4_INFO_HH

#include <utility>
#include <tuple>
#include <memory>

#include "mp4_parser.hh"

namespace MP4 {

class MP4Info
{
public:
  MP4Info(const std::shared_ptr<MP4Parser> & parser);

  std::tuple<uint32_t, uint64_t> get_timescale_duration();
  std::tuple<uint16_t, uint16_t> get_width_height();
  std::tuple<uint8_t, uint8_t> get_avc_profile_level();
  float get_fps(uint16_t frame_count = 1);
  float get_fps(uint32_t timescale, uint32_t duration,
                uint16_t frame_count = 1);
  uint16_t get_frame_per_sample();
  uint32_t get_bitrate();
  uint32_t get_bitrate(uint32_t timescale, uint32_t duration);
  bool is_video();
  bool is_audio();
  uint32_t get_sample_rate();
  std::pair<uint8_t, uint16_t> get_audio_code_channel();

private:
  std::shared_ptr<MP4Parser> parser_;
};

} /* namespace MP4 */

#endif /* MP4_INFO_HH */
