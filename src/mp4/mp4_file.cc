#include <fcntl.h>
#include <unistd.h>
#include <endian.h>

#include "exception.hh"
#include "mp4_file.hh"

using namespace std;
using namespace MP4;

MP4File::MP4File(const string & filename)
  : FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), O_RDONLY)))
{}

uint64_t MP4File::seek(const int64_t offset, const int whence)
{
  return CheckSystemCall("lseek", lseek(fd_num(), offset, whence));
}

uint64_t MP4File::curr_offset()
{
  return seek(0, SEEK_CUR);
}

uint64_t MP4File::inc_offset(const int64_t offset)
{
  return seek(offset, SEEK_CUR);
}

uint64_t MP4File::filesize()
{
  uint64_t prev_offset = curr_offset();
  uint64_t fsize = seek(0, SEEK_END);

  /* seek back to the previous offset */
  seek(prev_offset, SEEK_SET);

  return fsize;
}

void MP4File::reset()
{
  seek(0, SEEK_SET);
  set_eof(false);
}

uint8_t MP4File::read_uint8()
{
  string data = read(1);
  const uint8_t * size = reinterpret_cast<const uint8_t *>(data.c_str());
  return *size;
}

uint16_t MP4File::read_uint16()
{
  string data = read(2);
  const uint16_t * size = reinterpret_cast<const uint16_t *>(data.c_str());
  return be16toh(*size);
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

int16_t MP4File::read_int16()
{
  string data = read(2);
  const int16_t * size = reinterpret_cast<const int16_t *>(data.c_str());
  return be16toh(*size);
}

void MP4File::read_zeros(const size_t bytes)
{
  for (size_t i = 0; i < bytes; ++i) {
    if (read_uint8() != 0) {
      throw runtime_error("read non-zero byte");
    }
  }
}
