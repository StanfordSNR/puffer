#include "mp4_info.hh"
#include "mp4_parser.hh"
#include "mvhd_box.hh"
#include "stsd_box.hh"

using namespace std;
using namespace MP4;

MP4Info::MP4Info(const shared_ptr<MP4Parser> & parser)
  : parser_(parser)
{}

tuple<uint32_t, uint64_t> MP4Info::get_timescale_duration()
{
  auto box = parser_->find_first_box_of("mvhd");
  auto mvhd_box = static_pointer_cast<MvhdBox>(box);

  return make_tuple(mvhd_box->timescale(), mvhd_box->duration());
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
