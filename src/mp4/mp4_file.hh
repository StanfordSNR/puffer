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
  std::tuple<uint8_t, uint32_t> read_version_flags();
};

}

#endif /* MP4_FILE_HH */
