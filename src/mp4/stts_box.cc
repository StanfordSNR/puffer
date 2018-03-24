#include <iostream>

#include "stts_box.hh"

using namespace std;
using namespace MP4;

SttsBox::SttsBox(const uint64_t size, const string & type)
  : FullBox(size, type), entries_()
{}

uint32_t SttsBox::total_sample_count()
{
  uint32_t total_sample_count = 0;
  for (const auto & entry : entries_) {
    total_sample_count += entry.sample_count;
  }

  return total_sample_count;
}

void SttsBox::set_entries(vector<Entry> entries)
{
  entries_ = move(entries);
}

void SttsBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";

  cout << indent_str << "entry count " << entry_count() << endl;

  if (entry_count() == 0) {
    return;
  }

  cout << indent_str << "[#] count, sample duration" << endl;

  for (uint32_t i = 0; i < entry_count() and i < 5; ++i) {
    cout << indent_str << "[" << i << "] " << entries_[i].sample_count << ", "
         << entries_[i].sample_delta << endl;
  }

  if (entry_count() > 5) {
    cout << indent_str << "..." << endl;
  }
}

void SttsBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  uint32_t entry_count = mp4.read_uint32();

  for (uint32_t i = 0; i < entry_count; ++i) {
    uint32_t sample_count = mp4.read_uint32();
    uint32_t sample_delta = mp4.read_uint32();
    entries_.push_back({sample_count, sample_delta});
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
