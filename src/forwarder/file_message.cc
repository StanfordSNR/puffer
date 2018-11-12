#include "file_message.hh"

#include <endian.h>
#include <stdexcept>

using namespace std;

string put_field(const uint16_t n)
{
  const uint16_t network_order = htobe16(n);
  return string(reinterpret_cast<const char *>(&network_order),
                sizeof(network_order));
}

uint16_t get_uint16(const char * data)
{
  return be16toh(*reinterpret_cast<const uint16_t *>(data));
}

FileMsg::FileMsg(const string & str)
{
  const char * data = str.data();

  if (str.size() < sizeof(dst_path_len)) {
    throw runtime_error("FileMsg is too small to contain dst_path_len");
  }

  dst_path_len = get_uint16(data);
  dst_path = str.substr(sizeof(dst_path_len),
                        sizeof(dst_path_len) + dst_path_len);
}

string FileMsg::to_string() const
{
  return put_field(dst_path_len) + dst_path;
}
