#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>

#include "exception.hh"
#include "strict_conversions.hh"
#include "mp4_file.hh"

using namespace std;
using namespace MP4;

MP4File::MP4File(const string & filename)
  : FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), O_RDONLY, 0)))
{}

int MP4File::curr_offset()
{
  off_t curr_pos = CheckSystemCall("lseek", lseek(fd_num(), 0, SEEK_CUR));
  return narrow_cast<int>(curr_pos);
}

uint32_t MP4File::read_uint32()
{
  string data = read(4);
  const uint32_t * size = reinterpret_cast<const uint32_t *>(data.c_str());
  return ntohl(*size);
}
