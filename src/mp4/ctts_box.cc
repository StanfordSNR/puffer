#include <iostream>
#include <algorithm>

#include "ctts_box.hh"

using namespace std;
using namespace MP4;

CttsBox::CttsBox(const uint64_t size, const string & type)
  : FullBox(size, type), entries_()
{}

void CttsBox::print_structure(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "sample count " << sample_count() << endl;

  if (sample_count()) {
    uint32_t count = min(sample_count(), 5u);

    cout << indent_str << "[#] composition time offset" << endl;
    for (uint32_t i = 0; i < count; ++i) {
      cout << indent_str << "[" << i << "] " << entries_[i] << endl;
    }

    if (count < sample_count()) {
      cout << indent_str << "..." << endl;
    }
  }
}

void CttsBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  uint32_t entry_count = mp4.read_uint32();

  if (version() == 0) {
    for (uint32_t i = 0; i < entry_count; ++i) {
      uint32_t sample_count = mp4.read_uint32();
      uint32_t sample_offset = mp4.read_uint32();

      for (uint32_t j = 0; j < sample_count; ++j) {
        entries_.emplace_back(sample_offset);
      }
    }
  } else {
    for (uint32_t i = 0; i < entry_count; ++i) {
      uint32_t sample_count = mp4.read_uint32();
      int32_t sample_offset = mp4.read_int32();

      for (uint32_t j = 0; j < sample_count; ++j) {
        entries_.emplace_back(sample_offset);
      }
    }
  }

  check_data_left(mp4, data_size, init_offset);
}
