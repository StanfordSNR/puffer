#ifndef MP4_FILE_HH
#define MP4_FILE_HH

#include <cstdint>
#include <string>

#include "file_descriptor.hh"

namespace MP4 {

class MP4File : public FileDescriptor
{
public:
  MP4File(const std::string & filename);

  inline int64_t seek(int64_t offset, int whence);
  int64_t curr_offset();
  int64_t inc_offset(int64_t offset);
  int64_t filesize();

  uint32_t read_uint32();
  uint64_t read_uint64();

  void reset();
};

}

#endif /* MP4_FILE_HH */
