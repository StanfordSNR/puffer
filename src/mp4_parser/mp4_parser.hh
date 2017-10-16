#ifndef MP4_PARSER_HH
#define MP4_PARSER_HH

#include <string>

#include "mp4_file.hh"

namespace MP4 {

class Parser
{
public:
  Parser(const std::string & filename);

private:
  MP4File file_;
};

}

#endif /* MP4_PARSER_HH */
