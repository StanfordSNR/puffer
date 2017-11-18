#ifndef MP4_FILE_HH
#define MP4_FILE_HH

#include <fcntl.h>
#include <cstdint>
#include <string>
#include <tuple>

#include "file_descriptor.hh"

namespace MP4 {

class MP4File : public FileDescriptor
{
public:
  MP4File(const std::string & filename, int flags);
  MP4File(const std::string & filename, int flags, mode_t mode);

  /* read bytes from file and return meaningful data */
  uint8_t read_uint8();
  uint16_t read_uint16();
  uint32_t read_uint32();
  uint64_t read_uint64();
  int8_t read_int8();
  int16_t read_int16();
  int32_t read_int32();
  int64_t read_int64();

  /* write bytes to file */
  void write_uint8(const uint8_t data);
  void write_uint16(const uint16_t data);
  void write_uint32(const uint32_t data);
  void write_uint64(const uint64_t data);
  void write_int8(const int8_t data);
  void write_int16(const int16_t data);
  void write_int32(const int32_t data);
  void write_int64(const int64_t data);

  /* write 'bytes' bytes of zeros to file */
  void write_zeros(const size_t bytes);

  void write_string(const std::string & data, const size_t bytes);

  /* overwrite 'data' at 'offset' */
  void write_uint32_at(const uint32_t data, const uint64_t offset);
  void write_int32_at(const int32_t data, const uint64_t offset);
};

} /* namespace MP4 */

#endif /* MP4_FILE_HH */
