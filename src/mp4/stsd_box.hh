#ifndef STSD_BOX_HH
#define STSD_BOX_HH

#include "box.hh"

namespace MP4 {

class StsdBox : public FullBox
{
public:
  StsdBox(const uint64_t size, const std::string & type);

  void parse_data(MP4File & mp4, const uint64_t data_size);
};

class SampleEntry : public Box
{
public:
  SampleEntry(const uint64_t size, const std::string & type);

  /* accessors */
  uint16_t data_reference_index() { return data_reference_index_; }

  void parse_data(MP4File & mp4);

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
  uint16_t depth() { return depth_; }
  uint16_t frame_count() { return frame_count_; }

  void parse_data(MP4File & mp4);

private:
  uint16_t width_;
  uint16_t height_;
  uint32_t horizresolution_ = 0x00480000; /* 72 dpi */
  uint32_t vertresolution_ = 0x00480000; /* 72 dpi */
  uint16_t frame_count_ = 1;
  std::string compressorname_;
  uint16_t depth_ = 0x0018;
  /* optional boxes are not parsed */
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

  void parse_data(MP4File & mp4, const uint64_t data_size);
  void print_structure(const unsigned int indent = 0);

private:
  uint8_t configuration_version_ = 1;
  uint8_t avc_profile_;
  uint8_t avc_profile_compatibility_;
  uint8_t avc_level_;

  /* for avcC */
  uint32_t avcc_size_;
};

}

#endif /* STSD_BOX_HH */
