#ifndef WEBM_PARSER_HH
#define WEBM_PARSER_HH

#include <fcntl.h>
#include <unistd.h>
#include <set>
#include "file_descriptor.hh"
#include "exception.hh"

namespace webm
{

class WebmElement
{
public:
  WebmElement(uint64_t tag, std::string value) : tag_(tag), value_(value)
  {}

private:
  uint64_t tag_;
  std::string value_;
};

class BinaryReader
{
public:
  uint8_t read_uint8() { return read<uint8_t>(); }
  uint16_t read_uint16() { return read<uint16_t>(); }
  uint32_t read_uint32() { return read<uint32_t>(); }
  uint64_t read_uint64() { return read<uint64_t>(); }
  std::string read_bytes(uint64_t size) { return fd_.read(size); }

  uint64_t pos() { return seek(0, SEEK_CUR); }
  uint64_t size();
  BinaryReader(const std::string & filename)
    : fd_(FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), O_RDONLY))))
  {}

private:
  template<typename T> T read();
  FileDescriptor fd_;

  uint64_t seek(const int64_t offset, const int whence)
  {
    return CheckSystemCall("lseek", lseek(fd_.fd_num(), offset, whence));
  }
};

class WebmParser
{
public:
  WebmParser(const std::string & filename);


private:
  BinaryReader br_;

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
    0x000000B7     // CueTrackPositions
  };

}

#endif /* WEBM_PARSER_HH */
