#ifndef MP4_HH
#define MP4_HH

#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <tuple>

#include "file_descriptor.hh"

namespace MP4 {

class MP4File : public FileDescriptor
{
public:
  MP4File(const std::string & filename);

  /* manipulate file offset */
  int64_t seek(const int64_t offset, const int whence);
  int64_t curr_offset();
  int64_t inc_offset(const int64_t offset);

  int64_t filesize();

  /* reset file offset to the beginning and set EOF to false */
  void reset();

  /* read bytes from file and return meaningful data */
  uint8_t read_uint8();
  uint16_t read_uint16();
  uint32_t read_uint32();
  uint64_t read_uint64();
  std::tuple<uint8_t, uint32_t> read_version_flags();
};

class Box
{
public:
  Box(const uint64_t size, const std::string & type);
  virtual ~Box() {}

  /* accessors */
  uint64_t size() { return size_; }
  std::string type() { return type_; }

  /* parameter is a sink; use rvalue reference to save a "move" operation */
  void add_child(std::shared_ptr<Box> && child);

  std::vector<std::shared_ptr<Box>>::const_iterator children_begin();
  std::vector<std::shared_ptr<Box>>::const_iterator children_end();

  std::shared_ptr<Box> find_in_descendants(const std::string & type);

  virtual void print_structure(int indent = 0);

  virtual void parse_data(MP4File & mp4, const int64_t data_size);

private:
  uint64_t size_;
  std::string type_;

  std::vector<std::shared_ptr<Box>> children_;
};

/* ISO/IEC 14496-12:2015 Section 8.2.2 */
class MvhdBox : public Box
{
public:
  MvhdBox(const uint64_t size, const std::string & type);
  ~MvhdBox() {}

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }
  uint64_t creation_time() { return creation_time_; }
  uint64_t modificaiton_time() { return modification_time_; }
  uint32_t timescale() { return timescale_; }
  uint64_t duration() { return duration_; }

  void print_structure(int indent = 0);

  void parse_data(MP4File & mp4, const int64_t data_size);

private:
  uint8_t version_;
  uint32_t flags_;
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t timescale_;
  uint64_t duration_;
};

/* ISO/IEC 14496-12:2015 Section 8.16.3 */
class SidxBox: public Box
{
public:
  struct SidxReference {
    bool        reference_type;
    uint32_t    reference_size;
    uint32_t    segment_duration;
    bool        starts_with_SAP;
    uint8_t     SAP_type;
    uint32_t    SAP_delta;
  };

  SidxBox(const uint64_t size, const std::string & type);
  ~SidxBox() {}

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }
  uint32_t reference_id() { return reference_id_; }
  uint32_t timescale() { return timescale_; }
  uint64_t earlist_presentation_time() { return earlist_presentation_time_; }
  uint64_t first_offset() { return first_offset_; }
  uint16_t reserved() { return reserved_; }

  /* take rvalue and move it, rather than copy */
  void add_reference(SidxReference && ref);

  void print_structure(int indent = 0);

  void parse_data(MP4File & mp4, const int64_t data_size);

private:
  uint8_t version_;
  uint32_t flags_;
  uint32_t reference_id_;
  uint32_t timescale_;
  uint64_t earlist_presentation_time_;
  uint64_t first_offset_;
  uint16_t reserved_;

  std::vector<SidxReference> reference_list_;
};

class SampleEntry : public Box
{
public:
  SampleEntry(const uint32_t size, const std::string & type):
    Box(size, type), data_reference_index_()
  {}
  void parse_data(MP4File & file, const int64_t)
  {
    uint32_t reserved1 = file.read_uint32();
    uint16_t reserved2 = file.read_uint16();
    if(reserved1 != 0 and reserved2 != 0)
      throw std::runtime_error("Invalid SampleEntry data");
    data_reference_index_ = file.read_uint16();
  }
  uint16_t data_reference_index() { return data_reference_index_; }
private:
  uint16_t data_reference_index_;
};

class VisualSampleEntry: public SampleEntry
{
public:
  VisualSampleEntry(const uint32_t data_size, const std::string & type);
  uint16_t width() { return width_; }
  uint16_t height() { return height_; }
  std::string compressorname() { return compressorname_; }
  uint32_t horizresolution() { return horizresolution_; }
  uint32_t vertresolution() { return vertresolution_; }
  uint16_t depth() { return depth_; }
  uint16_t frame_count() { return frame_count_; }

  void parse_data(MP4File & file, const int64_t data_size);

private:
  uint16_t width_;
  uint16_t height_;
  std::string compressorname_;
  uint32_t horizresolution_ = 0x00480000; /* 72 dpi */
  uint32_t vertresolution_ = 0x00480000; /* 72 dpi */
  uint16_t depth_ = 0x0018;
  uint16_t frame_count_ = 1;
  uint16_t pre_defined_ = -1;
  /* optional boxes are not implemented */
};

class AVC1 : public VisualSampleEntry
{
public:
  AVC1(const uint32_t data_size, const std::string & type);

  uint8_t configuration_version() { return configuration_version_; }
  uint8_t avc_profile() { return avc_profile_; }
  uint8_t avc_profile_compatibility() { return avc_profile_compatibility_; }
  uint8_t avc_level() { return avc_level_; }

  void parse_data(MP4File & file, const int64_t data_size);
  void print_structure(int indent = 0);
private:
  uint8_t configuration_version_ = 1;
  uint8_t avc_profile_;
  uint8_t avc_profile_compatibility_;
  uint8_t avc_level_;

  /* for avcC */
  uint32_t avcc_size_;
};

class StsdBox: public Box
{
public:
  StsdBox(const uint64_t size, const std::string & type);

  /* accessors */
  uint8_t version() { return version_; }
  uint32_t flags() { return flags_; }

  void parse_data(MP4File & mp4, const int64_t data_size);

private:
  uint8_t version_;
  uint32_t flags_;
};

class Parser
{
public:
  Parser(const std::string & filename);

  /* parse MP4 into boxes */
  void parse();

  /* print MP4 structure, i.e., type and size of each box, and box hierarchy
   * call after parse() */
  void print_structure();

  /* split fragmented MP4 into initial segment and media segments
   * call after parse() */
  void split(const std::string & init_seg,
             const std::string & media_seg_template,
             const unsigned int start_number);

private:
  MP4File file_;
  std::shared_ptr<Box> box_;

  /* recursively create boxes between start_offset and its following total_size
   * add created boxes as children of parent_box */
  void create_boxes(const std::shared_ptr<Box> & parent_box,
                    const int64_t start_offset, const int64_t total_size);
  /* a factory method to create different boxes based on their type
   */
  void box_factory(std::shared_ptr<Box> & box, const std::string & type,
    const uint32_t & size, MP4File & file, const int64_t data_size);

  /* copy size_to_copy bytes from current offset and write to filename */
  void copy_to_file(const std::string & output_filename,
                    const int64_t size_to_copy);

  std::string populate_template(const std::string & media_seg_template,
                                const unsigned int curr_seg_number);
};

}

#endif /* MP4_HH */
