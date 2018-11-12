#ifndef FILE_MESSAGE_HH
#define FILE_MESSAGE_HH

#include <string>

class FileMsg
{
public:
  uint16_t dst_path_len {};
  std::string dst_path {};

  FileMsg(const uint16_t dst_path_len, const std::string & dst_path);

  /* parse a file message from network */
  FileMsg(const std::string & str);

  /* make network representation of file message */
  std::string to_string() const;

  unsigned int size() const;
};

#endif /* FILE_MESSAGE_HH */
