#include <iostream>
#include <stdexcept>

#include "sidx_box.hh"

using namespace std;
using namespace MP4;

SidxBox::SidxBox(const uint64_t size, const std::string & type)
  : FullBox(size, type), reference_id_(), timescale_(),
    earlist_presentation_time_(), first_offset_(),
    reserved_(), reference_list_()
{}

void SidxBox::add_reference(SidxReference && ref)
{
  reference_list_.emplace_back(move(ref));
}

uint32_t SidxBox::duration() {
  uint32_t result = 0;
  for (auto ref: reference_list_) {
    result += ref.subsegment_duration;
  }
  return result;
}

void SidxBox::print_structure(const unsigned int indent)
{
  cout << string(indent, ' ') << "- " << type() << " " << size() << endl;

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "reference id " << reference_id_ << endl;
  cout << indent_str << "timescale " << timescale_ << endl;

  cout << indent_str << "subsegment durations";
  for (const auto & ref : reference_list_) {
    cout << " " << ref.subsegment_duration;
  }
  cout << endl;
}

void SidxBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  FullBox::parse_data(mp4);

  reference_id_ = mp4.read_uint32();
  timescale_ = mp4.read_uint32();

  if (version() == 0) {
    earlist_presentation_time_ = mp4.read_uint32();
    first_offset_ = mp4.read_uint32();
  } else {
    earlist_presentation_time_ = mp4.read_uint64();
    first_offset_ = mp4.read_uint64();
  }

  mp4.read_zeros(2);

  uint16_t reference_count = mp4.read_uint16();
  for (unsigned int i = 0; i < reference_count; ++i) {
    uint32_t data = mp4.read_uint32();
    bool reference_type = (data >> 31) & 1;
    uint32_t reference_size = data & 0x7FFFFFFF;

    uint32_t subsegment_duration = mp4.read_uint32();

    data = mp4.read_uint32();
    bool starts_with_sap = (data >> 31) & 1;
    uint8_t sap_type = (data >> 28) & 7;
    uint32_t sap_delta = (data >> 4) & 0x0FFFFFFF;

    add_reference({
      reference_type, reference_size, subsegment_duration,
      starts_with_sap, sap_type, sap_delta
    });
  }

  check_data_left(mp4, data_size, init_offset);
}
