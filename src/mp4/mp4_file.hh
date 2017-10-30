#ifndef MP4_FILE_HH
#define MP4_FILE_HH

#include <string>
#include <tuple>
#include <cstdint>

#include "file_descriptor.hh"

namespace MP4 {

class MP4File : public FileDescriptor
{
public:
  MP4File(const std::string & filename);

  /* manipulate file offset */
  uint64_t seek(const int64_t offset, const int whence);
  uint64_t curr_offset();
  uint64_t inc_offset(const int64_t offset);

  uint64_t filesize();

  /* reset file offset to the beginning and set EOF to false */
  void reset();

  /* read bytes from file and return meaningful data */
  uint8_t read_uint8();
  uint16_t read_uint16();
  uint32_t read_uint32();
  uint64_t read_uint64();
  int8_t read_int8();
  int16_t read_int16();
  int32_t read_int32();
  int64_t read_int64();

  /* throw runtime error if any byte read is not zero */
  void read_zeros(const size_t bytes);

  /* write bytes to file */
  void write_uint8(const uint8_t data);
  void write_uint16(const uint16_t data);
  void write_uint32(const uint32_t data);
  void write_uint64(const uint64_t data);
  void write_int8(const int8_t data);
  void write_int16(const int16_t data);
  void write_int32(const int32_t data);
  void write_int64(const int64_t data);

  void write_zeros(const size_t bytes);
};

}

#endif /* MP4_FILE_HH */
