#include "file_message.hh"
#include "serialization.hh"

#include <endian.h>
#include <stdexcept>

using namespace std;

FileMsg::FileMsg(const uint16_t _dst_path_len, const string & _dst_path)
  : dst_path_len(_dst_path_len), dst_path(_dst_path)
{}

FileMsg::FileMsg(const string & str)
{
  const char * data = str.data();

  if (str.size() < sizeof(dst_path_len)) {
    throw runtime_error("FileMsg is too small to contain dst_path_len");
  }

  dst_path_len = get_uint16(data);
  dst_path = str.substr(sizeof(dst_path_len), dst_path_len);
}

string FileMsg::to_string() const
{
  return put_field(dst_path_len) + dst_path;
}

unsigned int FileMsg::size() const
{
  return sizeof(dst_path_len) + dst_path.size();
}
