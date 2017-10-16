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

  int curr_offset();

  uint32_t read_uint32();
};

}

#endif /* MP4_FILE_HH */
