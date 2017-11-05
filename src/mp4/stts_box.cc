#include <iostream>

#include "stts_box.hh"

using namespace std;
using namespace MP4;

SttsBox::SttsBox(const uint64_t size, const string & type)
  : FullBox(size, type), entries_()
{}

void SttsBox::set_entries(vector<Entry> entries)
{
  entries_ = move(entries);
}

void SttsBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "entry count " << entry_count() << endl;
}

void SttsBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  uint32_t entry_count = mp4.read_uint32();

  for (uint32_t i = 0; i < entry_count; ++i) {
    uint32_t sample_count = mp4.read_uint32();
    uint32_t sample_delta = mp4.read_uint32();
    entries_.emplace_back(Entry{sample_count, sample_delta});
  }

  check_data_left(mp4, data_size, init_offset);
}

void SttsBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(entry_count());

  for (const auto & entry : entries_) {
    mp4.write_uint32(entry.sample_count);
    mp4.write_uint32(entry.sample_delta);
  }

  fix_size_at(mp4, size_offset);
}
