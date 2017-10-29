#include "mp4_info.hh"
#include "mp4_parser.hh"
#include "mvhd_box.hh"
#include "stsd_box.hh"
#include "trun_box.hh"
#include "stsz_box.hh"
#include "sidx_box.hh"

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
  if (timescale == 0) {
    /* trying to find it in sidx */
    box = parser_->find_first_box_of("sidx");
    if (box != nullptr) {
      auto sidx_box = static_pointer_cast<SidxBox>(box);
      timescale = sidx_box->timescale();
    }
  }
  if (duration == 0) {
    /*  trun is out last resort */
    box = parser_->find_first_box_of("trun");
    if (box != nullptr) {
      auto trun_box = static_pointer_cast<TrunBox>(box);
      duration = trun_box->duration();
    }

  }
  return make_tuple(timescale, duration);
}

tuple<uint16_t, uint16_t> MP4Info::get_width_height()
{
  auto box = parser_->find_first_box_of("avc1");
  auto avc1_box = static_pointer_cast<AVC1>(box);

  return make_tuple(avc1_box->width(), avc1_box->height());
}

tuple<uint8_t, uint8_t> MP4Info::get_avc_profile_level()
{
  auto box = parser_->find_first_box_of("avc1");
  auto avc1_box = static_pointer_cast<AVC1>(box);

  return make_tuple(avc1_box->avc_profile(), avc1_box->avc_level());
}

float MP4Info::get_fps()
{

  auto box = parser_->find_first_box_of("trun");
  auto trun_box = static_pointer_cast<TrunBox>(box);
  uint64_t duration;
  tie(std::ignore, duration) = get_timescale_duration();
  if (duration == 0)
    return 0;
  else
    return ((float)trun_box->sample_count()) / duration;
}
