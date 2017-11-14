#ifndef WEBM_INFO_HH
#define WEBM_INFO_HH

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <set>
#include <vector>
#include <memory>
#include <iostream>

#include "file_descriptor.hh"
#include "exception.hh"
#include "strict_conversions.hh"

class BinaryReader
{
public:
  uint8_t read_uint8() { return read<uint8_t>(); }
  uint16_t read_uint16() { return read<uint16_t>(); }
  uint32_t read_uint32() { return read<uint32_t>(); }
  uint64_t read_uint64() { return read<uint64_t>(); }
  float read_float() { return read<float>(true); }
  double read_double() { return read<double>(true); }

  std::string read_bytes(uint64_t size) { return fd_.read(size); }

  uint64_t pos() { return seek(0, SEEK_CUR); }
  uint64_t size();
  BinaryReader(const std::string & filename, bool little_endian = true)
    : fd_(FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), O_RDONLY)))),
      little_endian_(little_endian)
  {}


private:
  template<typename T> T read(bool switch_endian = false);
  FileDescriptor fd_;
  bool little_endian_;

  uint64_t seek(const int64_t offset, const int whence)
  {
    return CheckSystemCall("lseek", lseek(fd_.fd_num(), offset, whence));
  }
};

class WebmElement : public std::enable_shared_from_this<WebmElement>
{
public:
  WebmElement(uint32_t tag, std::string value)
            : tag_(tag), size_(value.size()), value_(value)
  {}

  WebmElement(uint32_t tag, uint64_t size, BinaryReader & br)
            : tag_(tag), size_(size), value_()
  { value_ = br.read_bytes(size); }

  WebmElement(uint32_t tag, uint64_t size)
            : tag_(tag), size_(size), value_()
  {}

  WebmElement()
    : WebmElement(0, 0)
  {}

  void print()
  {
    std::cout << "Tag: 0x" << std::hex << tag_ << " Size: 0x" << size()
              << std::endl;
  }

  uint32_t tag() { return tag_; }
  uint64_t size() { return size_; }
  std::string value() { return value_; }
  std::set<std::shared_ptr<WebmElement>> get_children() { return children_; }
  void add_element(std::shared_ptr<WebmElement> elem)
  { children_.insert(elem); }

  std::shared_ptr<WebmElement> find_first(const uint32_t tag);
  std::set<std::shared_ptr<WebmElement>> find_all(const uint32_t tag);

protected:
  uint32_t tag_;
  uint64_t size_;
  std::string value_;
  std::set<std::shared_ptr<WebmElement>> children_ = { };
};

class WebmParser
{
public:
  WebmParser(const std::string & filename);
  std::shared_ptr<WebmElement> find_first(uint32_t tag);
  std::set<std::shared_ptr<WebmElement>> find_all(uint32_t tag);
  std::set<std::shared_ptr<WebmElement>> get_all()
  { return root_->get_children(); }

private:
  BinaryReader br_;
  std::shared_ptr<WebmElement> root_ = std::make_shared<WebmElement>();

  uint64_t scan_tag();
  uint64_t decode_bytes(uint32_t tag_size, uint8_t first, uint8_t first_mask);
  uint64_t scan_data_size();
  void parse(uint64_t max_pos, std::shared_ptr<WebmElement> parent);
};

enum ElementTagID {
    EBML                = 0x1A45DFA3,
    Segment             = 0x18538067,
    SeekHead            = 0x114D9B74,
    Seek                = 0x00004DBB,
    Info                = 0x1549A966,
    Cluster             = 0x1F43B675,
    BlockGroup          = 0x000000AD,
    Tracks              = 0x1654AE6B,
    TrackEntry          = 0x000000AE,
    Audio               = 0x000000E1,
    TrackOperation      = 0x000000E2,
    TrackCombinePlanes  = 0x000000E3,
    TrackPlane          = 0x000000E4,
    TrackJoinBlocks     = 0x000000E9,
    Cues                = 0x1C53BB6B,
    CueuePoint          = 0x000000BB,
    CueTrackPositions   = 0x000000B7,
    Tags                = 0x1254c367,
    Tag                 = 0x00007373,
    SimpleTag           = 0x000067c8,
    TimecodeScale       = 0x002ad7b1,
    TrackNumber         = 0x000000d7,
    TrackType           = 0x00000083,
    Timecode            = 0x000000e7,
    SamplingFrequency   = 0x000000b5,
    TagName             = 0x000045a3,
    TagString           = 0x00004487,
    SimpleBlock         = 0x000000a3,
};

const std::set<uint32_t> master_elements {
    EBML,
    Segment,
    SeekHead,
    Seek,
    Info,
    Cluster,
    BlockGroup,
    Tracks,
    TrackEntry,
    Audio,
    TrackOperation,
    TrackCombinePlanes,
    TrackPlane,
    TrackJoinBlocks,
    Cues,
    CueuePoint,
    CueTrackPositions,
    Tags,
    Tag,
    SimpleTag,
};


template<typename T>
T read_raw(std::string data, uint64_t size, bool switch_endian = true)
{
  const T * value = reinterpret_cast<const T *>(data.c_str());
  if (switch_endian) {
    T result;
    char * dst = reinterpret_cast<char *>(&result);
    memset(dst, 0, sizeof(T));
    const char * src = reinterpret_cast<const char *>(value);
    for (uint32_t i = 0; i < size; i++) {
      dst[size - i - 1] = src[i];
    }
    return result;
  } else {
    return *value;
  }
}

class WebmInfo
{
public:
  WebmInfo(const std::string & filename)
    : parser_(filename)
  {}

  uint32_t get_timescale();
  uint32_t get_duration(uint32_t timescale);
  uint32_t get_duration() { return get_duration(get_timescale()); }
  uint32_t get_bitrate() { return get_bitrate(get_timescale(),
                                              get_duration()); }
  uint32_t get_bitrate(uint32_t timescale, uint32_t duration);
  uint32_t get_sample_rate();

private:
  WebmParser parser_;
};

#endif /* WEBM_INFO_HH */
