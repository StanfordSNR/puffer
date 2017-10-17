#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <endian.h>

#include "exception.hh"
#include "strict_conversions.hh"
#include "mp4_file.hh"

using namespace std;
using namespace MP4;

MP4File::MP4File(const string & filename)
  : FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), O_RDONLY, 0)))
{}

inline int64_t MP4File::seek(int64_t offset, int whence)
{
  return CheckSystemCall("lseek", lseek(fd_num(), offset, whence));
}

int64_t MP4File::curr_offset()
{
  return seek(0, SEEK_CUR);
}

int64_t MP4File::inc_offset(int64_t offset)
{
  return seek(offset, SEEK_CUR);
}

int64_t MP4File::filesize()
{
  int64_t prev_offset = curr_offset();
  int64_t fsize = seek(0, SEEK_END); /* seek to end of file */
  seek(prev_offset, SEEK_SET); /* seek back to previous offset */
  return fsize;
}

uint32_t MP4File::read_uint32()
{
  string data = read(4);
  const uint32_t * size = reinterpret_cast<const uint32_t *>(data.c_str());
  return be32toh(*size);
}

uint64_t MP4File::read_uint64()
{
  string data = read(8);
  const uint64_t * size = reinterpret_cast<const uint64_t *>(data.c_str());
  return be64toh(*size);
}
