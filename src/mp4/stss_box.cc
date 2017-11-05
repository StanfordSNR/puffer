#include <iostream>

#include "stss_box.hh"

using namespace std;
using namespace MP4;

StssBox::StssBox(const uint64_t size, const string & type)
  : FullBox(size, type), entries_()
{}

void StssBox::set_entries(vector<uint32_t> entries)
{
  entries_ = move(entries);
}

void StssBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "entry count " << entry_count() << endl;
}

void StssBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  uint32_t entry_count = mp4.read_uint32();

  for (uint32_t i = 0; i < entry_count; ++i) {
    uint32_t sample_number = mp4.read_uint32();
    entries_.emplace_back(sample_number);
  }

  check_data_left(mp4, data_size, init_offset);
}

void StssBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(entry_count());

  for (const auto & sample_number : entries_) {
    mp4.write_uint32(sample_number);
  }

  fix_size_at(mp4, size_offset);
}
