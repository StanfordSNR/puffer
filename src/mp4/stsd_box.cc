#include <iostream>
#include <stdexcept>

#include "stsd_box.hh"

using namespace std;
using namespace MP4;

StsdBox::StsdBox(const uint64_t size, const string & type)
  : FullBox(size, type), ignored_boxes_()
{}

void StsdBox::ignore_sample_entry(const string & sample_type)
{
  ignored_boxes_.insert(sample_type);
}

bool StsdBox::is_ignored(const string & sample_type)
{
  return ignored_boxes_.find(sample_type) != ignored_boxes_.end();
}

void StsdBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  /* parse sample entries */
  uint32_t num_sample_entries = mp4.read_uint32();

  for (uint32_t i = 0; i < num_sample_entries; ++i) {
    uint32_t sample_size = mp4.read_uint32();
    string sample_type = mp4.read(4);

    shared_ptr<Box> box;

    if (is_ignored(sample_type)) {
      /* ignore parsing sample entry but save raw data */
      box = make_shared<Box>(sample_size, sample_type);
    } else {
      if (sample_type == "avc1") {
        box = make_shared<AVC1>(sample_size, sample_type);
      } else if (sample_type == "mp4a") {
        box = make_shared<MP4A>(sample_size, sample_type);
      } else {
        /* unknown sample entry type */
        box = make_shared<Box>(sample_size, sample_type);
      }
    }

    box->parse_data(mp4, sample_size - 8);
    add_child(move(box));
  }

  check_data_left(mp4, data_size, init_offset);
}

void StsdBox::write_box(MP4File & mp4)
{
  uint64_t size_offset = mp4.curr_offset();

  write_size_type(mp4);
  write_version_flags(mp4);

  mp4.write_uint32(num_sample_entries());

  for (auto it = children_begin(); it != children_end(); ++it) {
    (*it)->write_box(mp4);
  }

  fix_size_at(mp4, size_offset);
}

SampleEntry::SampleEntry(const uint64_t size, const string & type)
  : Box(size, type), data_reference_index_()
{}

void SampleEntry::parse_sample_entry(MP4File & mp4)
{
  mp4.read(6); /* reserved */
  data_reference_index_ = mp4.read_uint16();
}

VisualSampleEntry::VisualSampleEntry(const uint64_t size, const string & type)
  : SampleEntry(size, type), width_(), height_(), compressorname_()
{}

void VisualSampleEntry::parse_visual_sample_entry(MP4File & mp4)
{
  SampleEntry::parse_sample_entry(mp4);

  mp4.read(2); /* pre-defined */
  mp4.read(2); /* reserved */
  mp4.read(12); /* pre-defined */

  width_ = mp4.read_uint16();
  height_ = mp4.read_uint16();
  horizresolution_ = mp4.read_uint32();
  vertresolution_ = mp4.read_uint32();

  mp4.read(4); /* reserved */

  frame_count_ = mp4.read_uint16();

  /* read compressorname and ignore padding */
  uint8_t displayed_bytes = mp4.read_uint8();
  if (displayed_bytes > 0) { /* be sure to avoid reading 0! */
    compressorname_ = mp4.read(displayed_bytes);
  }
  mp4.read(31 - displayed_bytes);

  depth_ = mp4.read_uint16();

  mp4.read(2); /* pre-defined */
}

AVC1::AVC1(const uint64_t size, const string & type)
  : VisualSampleEntry(size, type), avc_profile_(),
    avc_profile_compatibility_(), avc_level_(), avcc_size_()
{}

void AVC1::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "width " << width() << endl;
  cout << indent_str << "height " << height() << endl;

  cout << string(indent + 2, ' ') << "- " << "avcC" << " " <<
          avcc_size_ << endl;

  indent_str = string(indent + 4, ' ') + "| ";
  cout << indent_str << "profile " << unsigned(avc_profile_) << endl;
  cout << indent_str << "level " << unsigned(avc_level_) << endl;
}

void AVC1::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  VisualSampleEntry::parse_visual_sample_entry(mp4);
  /* avcc is parsed along with avc1 */

  string type = "";
  for (int i = 0; i < 2; i++) {
    avcc_size_ = mp4.read_uint32();
    type = mp4.read(4);
    if (type != "avcC") {
      mp4.read(avcc_size_ - 8); /* we've read 8 bytes already */
    } else {
      break;
    }
  }

  if (type != "avcC") {
    throw runtime_error("avcC box not found");
  }

  configuration_version_ = mp4.read_uint8();
  avc_profile_ = mp4.read_uint8();
  avc_profile_compatibility_ = mp4.read_uint8();
  avc_level_ = mp4.read_uint8();

  skip_data_left(mp4, data_size, init_offset);
}

AudioSampleEntry::AudioSampleEntry(const uint64_t size, const string & type)
  : SampleEntry(size, type)
{}

void AudioSampleEntry::parse_audio_sample_entry(MP4File & mp4)
{
  SampleEntry::parse_sample_entry(mp4);

  mp4.read(2); /* entry_version */
  mp4.read(6); /* reserved */

  channel_count_ = mp4.read_uint16();
  sample_size_ = mp4.read_uint16();
  mp4.read(2); /* pre-defined */
  mp4.read(2); /* reserved */
  sample_rate_ = mp4.read_uint32() >> 16;
}

MP4A::MP4A(const uint64_t size, const string & type)
  : AudioSampleEntry(size, type), esds_box_()
{}

void MP4A::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  AudioSampleEntry::parse_audio_sample_entry(mp4);

  /* assume there are no other boxes in between */
  uint32_t size = mp4.read_uint32();
  string type = mp4.read(4);

  if (type != "esds") {
    throw runtime_error("expect esds box inside mp4a box");
  }

  esds_box_ = make_shared<EsdsBox>(size, type);
  esds_box_->parse_data(mp4, size - 8);

  skip_data_left(mp4, data_size, init_offset);
}

void MP4A::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "channel count " << channel_count() << endl;
  cout << indent_str << "sample size " << sample_size() << endl;
  cout << indent_str << "sample rate " << sample_rate() << endl;

  esds_box_->print_box(indent + 2);
}

EsdsBox::EsdsBox(const uint64_t size, const string & type)
  : FullBox(size, type), es_id_(), stream_priority_(), object_type_(),
    max_bitrate_(), avg_bitrate_()
{}

void EsdsBox::parse_data(MP4File & mp4, const uint64_t data_size)
{
  uint64_t init_offset = mp4.curr_offset();

  parse_version_flags(mp4);

  if (mp4.read_uint8() != type_tag) {
    throw runtime_error("expect esds to have a type tag");
  }
  read_tag_string(mp4);

  mp4.read(1); /* descriptor type length */

  es_id_ = mp4.read_uint16();
  uint8_t bits = mp4.read_uint8();
  uint8_t flags = (bits >> 5) & 0x7;
  stream_priority_ = bits & 0x1F;
  if (flags & flag_stream_dependency) {
    mp4.read_uint16();
  }
  if (flags & flag_url) {
    uint8_t length = mp4.read_uint8();
    if (length) {
      mp4.read(length - 1); /* skip all the data */
    }
  }

  if (flags & flag_ocr_stream) {
    mp4.read_uint16();
  }
  if (mp4.read_uint8() != config_type_tag) {
    throw runtime_error("expect config type tag");
  }
  read_tag_string(mp4);

  mp4.read(1); /* descriptor type length */

  object_type_ = mp4.read_uint8();

  mp4.read(1); /* stream type, upstream flag, reserved flag */
  mp4.read(3); /* buffer size */

  max_bitrate_ = mp4.read_uint32();
  avg_bitrate_ = mp4.read_uint32();

  skip_data_left(mp4, data_size, init_offset);
}

void EsdsBox::read_tag_string(MP4File & mp4)
{
  uint64_t init_offset = mp4.curr_offset();

  for (int i = 0; i < 3; i++) {
    uint8_t tag = mp4.read_uint8();

    if (tag != tag_string_start and tag != tag_string_end) {
      /* no optional start and end flags */
      mp4.seek(init_offset, SEEK_SET);
      break;
    }
  }
}

void EsdsBox::print_box(const unsigned int indent)
{
  print_size_type(indent);

  string indent_str = string(indent + 2, ' ') + "| ";
  cout << indent_str << "object type " << unsigned(object_type_) << endl;
  cout << indent_str << "max bitrate " << max_bitrate_ << endl;
  cout << indent_str << "avg bitrate " << avg_bitrate_ << endl;
}
