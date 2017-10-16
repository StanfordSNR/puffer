#include <iostream>

#include "mp4_parser.hh"

using namespace std;
using namespace MP4;

Parser::Parser(const string & filename)
  : file_(filename)
{}
