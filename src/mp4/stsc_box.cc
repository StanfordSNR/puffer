#include <iostream>

#include "stsc_box.hh"

using namespace std;
using namespace MP4;

StscBox::StscBox(const uint64_t size, const string & type)
  : FullBox(size, type), entries_()
{}

void StscBox::set_entries(vector<Entry> entries)
{
  entries_ = move(entries);
}

void StscBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "entry count " << entry_count() << endl;

  if (entry_count() == 0) {
    return;
  }

  cout << indent_str << "[#] first chunk, samples per chunk" << endl;
  for (uint32_t i = 0; i < entry_count() and i < 5; ++i) {
    cout << indent_str << "[" << i << "] " << entries_[i].first_chunk
         << ", " << entries_[i].samples_per_chunk << endl;
  }

  if (entry_count() > 5) {
    cout << indent_str << "..." << endl;
  }
}

void StscBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  uint32_t entry_count = mp4.read_uint32();

  for (uint32_t i = 0; i < entry_count; ++i) {
    uint32_t first_chunk = mp4.read_uint32();
    uint32_t samples_per_chunk = mp4.read_uint32();
    uint32_t sample_description_index = mp4.read_uint32();

    entries_.push_back({first_chunk, samples_per_chunk,
                        sample_description_index});
  }

  check_data_left(mp4, data_size, init_offset);
}

void StscBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(entry_count());

  for (const auto & entry : entries_) {
    mp4.write_uint32(entry.first_chunk);
    mp4.write_uint32(entry.samples_per_chunk);
    mp4.write_uint32(entry.sample_description_index);
  }

  fix_size_at(mp4, size_offset);
}
