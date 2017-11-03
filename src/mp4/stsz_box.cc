#include <iostream>
#include <algorithm>

#include "stsz_box.hh"
#include "strict_conversions.hh"

using namespace std;
using namespace MP4;

StszBox::StszBox(const uint64_t size, const string & type)
  : FullBox(size, type), sample_size_(), entries_()
{}

StszBox::StszBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 const uint32_t sample_size,
                 std::vector<uint32_t> entries)
  : FullBox(type, version, flags), sample_size_(sample_size),
    entries_(move(entries))
{}

void StszBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "sample count " << sample_count() << endl;

  if (sample_count()) {
    uint32_t count = min(sample_count(), 5u);

    cout << indent_str << "[#] sample size" << endl;
    for (uint32_t i = 0; i < count; ++i) {
      cout << indent_str << "[" << i << "] " << entries_[i] << endl;
    }

    if (count < sample_count()) {
      cout << indent_str << "..." << endl;
    }
  } else if (sample_size_ > 0) {
    cout << indent_str << "sample size " << sample_size_ << endl;
  }
}

void StszBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  sample_size_ = mp4.read_uint32();
  uint32_t sample_count = mp4.read_uint32();

  if (sample_size_ == 0) {
    for (uint32_t i = 0; i < sample_count; ++i) {
      entries_.emplace_back(mp4.read_uint32());
    }
  }

  check_data_left(mp4, data_size, init_offset);
}

void StszBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(sample_size_);
  mp4.write_uint32(entries_.size());

  if (sample_size_ == 0) {
    for (const auto entry_size : entries_) {
      mp4.write_uint32(entry_size);
    }
  }

  fix_size_at(mp4, size_offset);
}
