#include <fcntl.h>
#include <unistd.h>
#include <endian.h>

#include "exception.hh"
#include "mp4_file.hh"

using namespace std;
using namespace MP4;

MP4File::MP4File(const string & filename, int flags)
  : FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), flags)))
{}

MP4File::MP4File(const string & filename, int flags, mode_t mode)
  : FileDescriptor(CheckSystemCall("open (" + filename + ")",
                                   open(filename.c_str(), flags, mode)))
{}

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

int8_t MP4File::read_int8()
{
  string data = read(1);
  const int8_t * size = reinterpret_cast<const int8_t *>(data.c_str());
  return *size;
}

int16_t MP4File::read_int16()
{
  string data = read(2);
  const int16_t * size = reinterpret_cast<const int16_t *>(data.c_str());
  return be16toh(*size);
}

int32_t MP4File::read_int32()
{
  string data = read(4);
  const int32_t * size = reinterpret_cast<const int32_t *>(data.c_str());
  return be32toh(*size);
}

int64_t MP4File::read_int64()
{
  string data = read(8);
  const int64_t * size = reinterpret_cast<const int64_t *>(data.c_str());
  return be64toh(*size);
}

void MP4File::write_uint8(const uint8_t data)
{
  write(string(1, static_cast<char>(data)));
}

void MP4File::write_uint16(const uint16_t data)
{
  uint16_t host_data = htobe16(data);
  const char * str = reinterpret_cast<const char *>(&host_data);
  write(string(str, 2));
}

void MP4File::write_uint32(const uint32_t data)
{
  const uint32_t host_data = htobe32(data);
  const char * str = reinterpret_cast<const char *>(&host_data);
  write(string(str, 4));
}

void MP4File::write_uint64(const uint64_t data)
{
  uint64_t host_data = htobe64(data);
  const char * str = reinterpret_cast<const char *>(&host_data);
  write(string(str, 8));
}

void MP4File::write_int8(const int8_t data)
{
  write(string(1, static_cast<char>(data)));
}

void MP4File::write_int16(const int16_t data)
{
  int16_t host_data = htobe16(data);
  const char * str = reinterpret_cast<const char *>(&host_data);
  write(string(str, 2));
}

void MP4File::write_int32(const int32_t data)
{
  int32_t host_data = htobe32(data);
  const char * str = reinterpret_cast<const char *>(&host_data);
  write(string(str, 4));
}

void MP4File::write_int64(const int64_t data)
{
  int64_t host_data = htobe64(data);
  const char * str = reinterpret_cast<const char *>(&host_data);
  write(string(str, 8));
}

void MP4File::write_zeros(const size_t bytes)
{
  for (size_t i = 0; i < bytes; ++i) {
    write(string(1, static_cast<char>(0)));
  }
}

void MP4File::write_string(const string & data, const size_t bytes)
{
  if (data.size() != bytes) {
    throw runtime_error("data size != bytes");
  }

  write(data);
}

void MP4File::write_uint32_at(const uint32_t data, const uint64_t offset)
{
  uint64_t prev_offset = curr_offset();
  seek(offset, SEEK_SET);
  write_uint32(data);
  seek(prev_offset, SEEK_SET);
}

void MP4File::write_int32_at(const int32_t data, const uint64_t offset)
{
  uint64_t prev_offset = curr_offset();
  seek(offset, SEEK_SET);
  write_int32(data);
  seek(prev_offset, SEEK_SET);
}
