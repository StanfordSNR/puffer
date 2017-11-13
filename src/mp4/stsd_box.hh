#ifndef STSD_BOX_HH
#define STSD_BOX_HH

#include <string>
#include <vector>
#include <set>
#include <memory>

#include "box.hh"

namespace MP4 {

class StsdBox : public FullBox
{
public:
  StsdBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint32_t num_sample_entries() { return children_size(); }

  void ignore_sample_entry(const std::string & sample_type);
  bool is_ignored(const std::string & sample_type);

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void write_box(MP4File & mp4);

private:
  std::set<std::string> ignored_boxes_;
};

class SampleEntry : public Box
{
public:
  SampleEntry(const uint64_t size, const std::string & type);

  /* accessors */
  uint16_t data_reference_index() { return data_reference_index_; }

  void parse_sample_entry(MP4File & mp4);

private:
  uint16_t data_reference_index_;
};

class VisualSampleEntry : public SampleEntry
{
public:
  VisualSampleEntry(const uint64_t size, const std::string & type);

  /* accessors */
  uint16_t width() { return width_; }
  uint16_t height() { return height_; }
  std::string compressorname() { return compressorname_; }
  uint32_t horizresolution() { return horizresolution_; }
  uint32_t vertresolution() { return vertresolution_; }
  uint16_t frame_count() { return frame_count_; }
  uint16_t depth() { return depth_; }

  void parse_visual_sample_entry(MP4File & mp4);

private:
  uint16_t width_;
  uint16_t height_;
  std::string compressorname_;
  uint32_t horizresolution_ = 0x00480000; /* 72 dpi */
  uint32_t vertresolution_ = 0x00480000; /* 72 dpi */
  uint16_t frame_count_ = 1;
  uint16_t depth_ = 0x0018;
};

class AVC1 : public VisualSampleEntry
{
public:
  AVC1(const uint64_t size, const std::string & type);

  /* accessors */
  uint8_t configuration_version() { return configuration_version_; }
  uint8_t avc_profile() { return avc_profile_; }
  uint8_t avc_profile_compatibility() { return avc_profile_compatibility_; }
  uint8_t avc_level() { return avc_level_; }

  void print_box(const unsigned int indent = 0);
  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  uint8_t configuration_version_ = 1;
  uint8_t avc_profile_;
  uint8_t avc_profile_compatibility_;
  uint8_t avc_level_;

  /* for avcC */
  uint32_t avcc_size_;
};

class AudioSampleEntry : public SampleEntry
{
public:
  AudioSampleEntry(const uint64_t size, const std::string & type);

  /* accessors */
  uint16_t channel_count() { return channel_count_; }
  uint16_t sample_size() { return sample_size_; }
  uint32_t sample_rate() { return sample_rate_; }

  void parse_audio_sample_entry(MP4File & mp4);

private:
  uint16_t channel_count_ = 2;
  uint16_t sample_size_ = 16;
  uint32_t sample_rate_ = 1 << 16;
};

/* ISO 14496-1 is not free in public domain */
class EsdsBox : public FullBox
{
public:
  EsdsBox(const uint64_t size, const std::string & type);

  uint16_t es_id() { return es_id_; }
  uint8_t stream_priority() { return stream_priority_; }
  uint8_t object_type() { return object_type_; }
  uint32_t max_bitrate() { return max_bitrate_; }
  uint32_t avg_bitrate() { return avg_bitrate_; }

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void print_box(const unsigned int indent = 0);

private:
  uint16_t es_id_;
  uint8_t stream_priority_;
  uint8_t object_type_;
  uint32_t max_bitrate_;
  uint32_t avg_bitrate_;

  static const uint8_t type_tag = 0x03;
  static const uint8_t config_type_tag = 0x04;
  static const uint8_t tag_string_start = 0x80;
  static const uint8_t tag_string_end = 0xFE;
  static const uint8_t flag_stream_dependency = 1;
  static const uint8_t flag_url               = 2;
  static const uint8_t flag_ocr_stream        = 4;

  void read_tag_string(MP4File & file);
};

class MP4A : public AudioSampleEntry
{
public:
  MP4A(const uint64_t size, const std::string & type);

  /* accessors */
  std::shared_ptr<EsdsBox> esds_box() { return esds_box_; }

  void print_box(const uint32_t indent = 0);
  void parse_data(MP4File & mp4, const uint64_t data_size);

private:
  std::shared_ptr<EsdsBox> esds_box_;
};

} /* namespace MP4 */

#endif /* STSD_BOX_HH */
