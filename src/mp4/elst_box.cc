#include <iostream>

#include "elst_box.hh"

using namespace std;
using namespace MP4;

ElstBox::ElstBox(const uint64_t size, const std::string & type)
  : FullBox(size, type), edit_list_()
{}

void ElstBox::print_structure(const unsigned int indent)
{
  print_size_type(indent);

  if (edit_list_.empty()) {
    return;
  }

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "segment durations";
  for (const auto & edit : edit_list_) {
    cout << " " << edit.segment_duration;
  }
  cout << endl;
}

void ElstBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  uint32_t entry_count = mp4.read_uint32();

  for (uint32_t i = 0; i < entry_count; ++i) {
    uint64_t segment_duration;
    int64_t media_time;

    if (version() == 1) {
      segment_duration = mp4.read_uint64();
      media_time = mp4.read_int64();
    } else {
      segment_duration = mp4.read_uint32();
      media_time = mp4.read_int32();
    }

    int16_t media_rate_integer = mp4.read_int16();
    int16_t media_rate_fraction = mp4.read_int16();

    edit_list_.emplace_back(Edit{segment_duration, media_time,
                                 media_rate_integer, media_rate_fraction});
  }

  check_data_left(mp4, data_size, init_offset);
}
