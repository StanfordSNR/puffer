#include <cmath>
#include "mp4_info.hh"
#include "mp4_parser.hh"
#include "mvhd_box.hh"
#include "stsd_box.hh"
#include "trun_box.hh"
#include "stsz_box.hh"
#include "sidx_box.hh"
#include "tfhd_box.hh"
#include "mdhd_box.hh"

using namespace std;
using namespace MP4;

MP4Info::MP4Info(const shared_ptr<MP4Parser> & parser)
  : parser_(parser)
{}

tuple<uint32_t, uint64_t> MP4Info::get_timescale_duration()
{
  /* this calculation is complicated due to the fragmentation */
  /* we first try mvhd to get what we need */
  uint32_t timescale = 0;
  uint64_t duration = 0;
  auto box = parser_->find_first_box_of("mvhd");
  if (box != nullptr) {
    auto mvhd_box = static_pointer_cast<MvhdBox>(box);
    timescale = mvhd_box->timescale();
    duration = mvhd_box->duration();
  }
  /* mdhd can override mvhd */
  box = parser_->find_first_box_of("mdhd");
  if (box != nullptr) {
    auto mdhd_box = static_pointer_cast<MdhdBox>(box);
    timescale = mdhd_box->timescale();
  }
  /* sidx can override as well */
  box = parser_->find_first_box_of("sidx");
  if (box != nullptr) {
    /* trying to find it in sidx */
    auto sidx_box = static_pointer_cast<SidxBox>(box);
    timescale = sidx_box->timescale();
    if (sidx_box->duration() != 0) {
      /* trying to fix duration as well if possible */
      duration = sidx_box->duration();
    }
  }
  if (duration == 0) {
    box = parser_->find_first_box_of("trun");
    auto trun_box = static_pointer_cast<TrunBox>(box);
    if (trun_box != nullptr) {
      duration = trun_box->total_sample_duration();
    }
    /* default sample duration
     * if this still doesn't work, it's impossible to find the duration
     */
    if (duration == 0) {
      box = parser_->find_first_box_of("tfhd");
      auto tfhd_box = static_pointer_cast<TfhdBox>(box);
      if (box != nullptr) {
        uint32_t sample_duration = tfhd_box->default_sample_duration();
        uint32_t sample_count = trun_box->sample_count();
        duration = sample_duration * sample_count;
      }
    }
  }
  return make_tuple(timescale, duration);
}

tuple<uint16_t, uint16_t> MP4Info::get_width_height()
{
  auto box = parser_->find_first_box_of("avc1");
  if (box == nullptr) {
    /* box not found */
    return make_tuple(0, 0);
  }
  auto avc1_box = static_pointer_cast<AVC1>(box);
  return make_tuple(avc1_box->width(), avc1_box->height());
}

tuple<uint8_t, uint8_t> MP4Info::get_avc_profile_level()
{
  auto box = parser_->find_first_box_of("avc1");
  if (box == nullptr) {
    /* box not found */
    return make_tuple(0, 0);
  }
  auto avc1_box = static_pointer_cast<AVC1>(box);
  return make_tuple(avc1_box->avc_profile(), avc1_box->avc_level());
}

uint16_t MP4Info::get_frame_per_sample()
{
  auto box = parser_->find_first_box_of("avc1");
  if (box == nullptr) {
    return 1; /* 1 is the default and saftest value */
  } else {
    auto avc1_box = static_pointer_cast<AVC1>(box);
    return avc1_box->frame_count();
  }
}

float MP4Info::get_fps(
    uint32_t timescale, uint32_t duration, uint16_t frame_count)
{
  auto box = parser_->find_first_box_of("trun");
  if (box == nullptr) {
    return 0;
  }
  auto trun_box = static_pointer_cast<TrunBox>(box);

  if (duration == 0) {
    /* no divided by 0 */
    return 0;
  } else {
    return static_cast<float>(trun_box->sample_count()) * timescale *
           frame_count / duration;
  }
}

float MP4Info::get_fps(uint16_t frame_count)
{
  uint64_t duration;
  uint32_t timescale;
  tie(timescale, duration) = get_timescale_duration();
  return get_fps(timescale, duration, frame_count);
}

bool MP4Info::is_video()
{
  return parser_->is_video();
}

bool MP4Info::is_audio()
{
  return parser_->is_audio();
}

uint32_t MP4Info::get_bitrate(uint32_t timescale, uint32_t duration)
{
  auto box = parser_->find_first_box_of("trun");
  if (box == nullptr) {
    return 0;
  }
  auto trun_box = static_pointer_cast<TrunBox>(box);
  if (timescale == 0) {
    return 0;
  }
  float s_duration =  static_cast<float>(duration) / timescale; /* seconds */
  if (s_duration == 0) {
    return 0;
  } else {
    /* round to nearest thousands */
    float raw_bitrate = trun_box->total_sample_size() / s_duration * 8;
    uint32_t bitrate = static_cast<uint32_t>(raw_bitrate);
    return (bitrate / 1000) * 1000;
  }
}

uint32_t MP4Info::get_bitrate()
{
  uint64_t duration;
  uint32_t timescale;
  tie(timescale, duration) = get_timescale_duration();
  return get_bitrate(timescale, duration);
}

uint32_t MP4Info::get_sample_rate()
{
  auto box = parser_->find_first_box_of("mp4a");
  if (box == nullptr) {
    return 0;
  }
  auto audio_box = static_pointer_cast<AudioSampleEntry>(box);
  return audio_box->sample_rate();
}

pair<uint8_t, uint16_t> MP4Info::get_audio_code_channel()
{
  auto box = parser_->find_first_box_of("mp4a");
  if (box == nullptr) {
    return make_pair(0, 0);
  }
  auto mp4a_box = static_pointer_cast<MP4A>(box);
  auto esds_box = mp4a_box->esds_box();
  if (esds_box == nullptr) {
    return make_pair(0, 0);
  }
  return make_pair(esds_box->object_type(), mp4a_box->channel_count());
}
