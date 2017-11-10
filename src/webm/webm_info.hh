#ifndef WEBM_INFO_HH
#define WEBM_INFO_HH

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
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

class WebmElement
{
public:
  WebmElement(uint32_t tag, std::string value)
            : tag_(tag), value_(value), size_(value.size())
  {}

  WebmElement(uint32_t tag, uint64_t size, BinaryReader & br)
            : tag_(tag), size_(size), value_()
  { value_ = br.read_bytes(size); }

  WebmElement(uint32_t tag, uint64_t size)
            : tag_(tag), size_(size), value_()
  {}

  virtual void print()
  {
    std::cout << "Tag: 0x" << std::hex << tag_ << " Size: 0x" << size()
              << std::endl;
  }

  uint32_t tag() { return tag_; }
  uint64_t size() { return size_; }
  std::string value() { return value_; }

protected:
  uint32_t tag_;
  uint64_t size_;
  std::string value_;
};

class WebmParser
{
public:
  WebmParser(const std::string & filename);
  std::shared_ptr<WebmElement> find_frst_elem(uint32_t tag);
  std::vector<std::shared_ptr<WebmElement>> find_all_elem(uint32_t tag);
  void print();
  std::vector<std::shared_ptr<WebmElement>> get_elements()
  { return elements_; }

private:
  BinaryReader br_;
  std::vector<std::shared_ptr<WebmElement>> elements_;

  uint64_t scan_tag();
  uint64_t decode_bytes(uint32_t tag_size, uint8_t first, uint8_t first_mask);
  uint64_t scan_data_size();
  void parse(uint64_t max_pos);
};

const std::set<uint32_t> master_elements {
    0x1A45DFA3,     // EBML
    0x18538067,     // Segment
    0x114D9B74,     // SeekHead
    0x00004DBB,     // Seek
    0x1549A966,     // Info
    0x1F43B675,     // Cluster
    0x000000AD,     // BlockGroup
    0x1654AE6B,     // Tracks
    0x000000AE,     // TrackEntry
    0x000000E1,     // Audio
    0x000000E2,     // TrackOperation
    0x000000E3,     // TrackCombinePlanes
    0x000000E4,     // TrackPlane
    0x000000E9,     // TrackJoinBlocks
    0x1C53BB6B,     // Cues
    0x000000BB,     // CuePoint
    0x000000B7      // CueTrackPositions
};


template<typename T>
T read_raw(std::string data, uint64_t size, bool switch_endian = true)
{
  std::string raw_data = data;
  uint32_t s = narrow_cast<uint32_t>(size);
  uint32_t data_size = sizeof(T);
  const T * value = reinterpret_cast<const T *>(data.c_str());
  if (switch_endian) {
    T result;
    char * dst = reinterpret_cast<char *>(&result);
    memset(dst, 0, sizeof(T));
    const char * src = reinterpret_cast<const char *>(value);
    for(int i = 0; i < size; i++) {
      dst[size - i - 1] = src[i];
    }
    return result;
  } else {
    return *value;
  }
}

#endif /* WEBM_INFO_HH */
