#include <iostream>
#include <algorithm>

#include "trun_box.hh"
#include "strict_conversions.hh"

using namespace std;
using namespace MP4;

TrunBox::TrunBox(const uint64_t size, const string & type)
  : FullBox(size, type), samples_()
{}

TrunBox::TrunBox(const string & type,
                 const uint8_t version,
                 const uint32_t flags,
                 /* 'samples': lvalue is copied, rvalue is moved */
                 vector<Sample> samples,
                 const int32_t data_offset,
                 const uint32_t first_sample_flags)
  : FullBox(type, version, flags), samples_(move(samples))
{
  if (flags & data_offset_present) {
    data_offset_ = data_offset;
  }
  if (flags & first_sample_flags_present) {
    first_sample_flags_ = first_sample_flags;
  }
}

uint64_t TrunBox::total_sample_duration()
{
  uint64_t total_sample_duration = 0;

  for (const auto & sample : samples_) {
    total_sample_duration += sample.sample_duration;
  }

  return total_sample_duration;
}

uint64_t TrunBox::total_sample_size()
{
  uint64_t total_sample_size = 0;

  for (const auto & sample : samples_) {
    total_sample_size += sample.sample_size;
  }

  return total_sample_size;
}

void TrunBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "sample count " << sample_count() << endl;

  if (flags() & data_offset_present) {
    cout << indent_str << "data offset " << data_offset_ << endl;
  }
  if (flags() & first_sample_flags_present) {
    cout << indent_str << "first sample flags " << hex << first_sample_flags_
         << dec << endl;
  }

  if (sample_count()) {
    uint32_t count = min(sample_count(), 5u);

    cout << indent_str << "[#] size, composition time offset" << endl;
    for (uint32_t i = 0; i < count; ++i) {
      cout << indent_str << "[" << i << "] " << samples_[i].sample_size
           << ", " << samples_[i].sample_composition_time_offset << endl;
    }

    if (count < sample_count()) {
      cout << indent_str << "..." << endl;
    }
  }

  cout << indent_str << "total sample duration "
                     << total_sample_duration() << endl;
  cout << indent_str << "total sample size " << total_sample_size() << endl;
}

void TrunBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  uint32_t sample_count = mp4.read_uint32();

  if (flags() & data_offset_present) {
    data_offset_ = mp4.read_int32();
  }

  if (flags() & first_sample_flags_present) {
    first_sample_flags_ = mp4.read_uint32();
  }

  for (uint32_t i = 0; i < sample_count; ++i) {
    uint32_t sample_duration = 0;
    uint32_t sample_size = 0;
    uint32_t sample_flags = 0;
    int64_t sample_composition_time_offset = 0;

    if (flags() & sample_duration_present) {
      sample_duration = mp4.read_uint32();
    }

    if (flags() & sample_size_present) {
      sample_size = mp4.read_uint32();
    }

    if (flags() & sample_flags_present) {
      sample_flags = mp4.read_uint32();
    }

    if (flags() & sample_composition_time_offsets_present) {
      if (version() == 0) {
        sample_composition_time_offset = mp4.read_uint32();
      } else {
        sample_composition_time_offset = mp4.read_int32();
      }
    }

    samples_.emplace_back(Sample{sample_duration, sample_size, sample_flags,
                                 sample_composition_time_offset});
  }

  check_data_left(mp4, data_size, init_offset);
}

void TrunBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(sample_count());

  if (flags() & data_offset_present) {
    mp4.write_int32(data_offset_);
  }

  if (flags() & first_sample_flags_present) {
    mp4.write_uint32(first_sample_flags_);
  }

  for (const auto & sample : samples_) {
    if (flags() & sample_duration_present) {
      mp4.write_uint32(sample.sample_duration);
    }

    if (flags() & sample_size_present) {
      mp4.write_uint32(sample.sample_size);
    }

    if (flags() & sample_flags_present) {
      mp4.write_uint32(sample.sample_flags);
    }

    if (flags() & sample_composition_time_offsets_present) {
      if (version() == 0) {
        mp4.write_uint32(
            narrow_cast<uint32_t>(sample.sample_composition_time_offset));
      } else {
        mp4.write_int32(
            narrow_cast<int32_t>(sample.sample_composition_time_offset));
      }
    }
  }

  fix_size_at(mp4, size_offset);
}
