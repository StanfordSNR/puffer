#include "y4m.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <fstream>

#include "exception.hh"
#include "tokenize.hh"

using namespace std;

Y4MParser::Y4MParser(const string & y4m_path)
  : width_(-1), height_(-1), frame_rate_numerator_(-1),
    frame_rate_denominator_(-1), interlaced_(false)
{
  ifstream y4m_file(y4m_path);
  string line;
  getline(y4m_file, line);

  /* split the first line into parameters */
  vector<string> params = split(line, " ");

  /* validate the Y4M file */
  if (params[0] != "YUV4MPEG2") {
    throw runtime_error(y4m_path + ": no YUV4MPEG2 found");
  }

  for (size_t i = 1; i < params.size(); ++i) {
    const string & p = params[i];
    size_t pos;

    switch (p.at(0)) {
    case 'W':
      width_ = stoi(p.substr(1));
      break;
    case 'H':
      height_ = stoi(p.substr(1));
      break;
    case 'F':
      pos = p.find(':');
      if (pos != string::npos) {
        frame_rate_numerator_ = stoi(p.substr(1, pos - 1));
        frame_rate_denominator_ = stoi(p.substr(pos + 1));
      }
      break;
    case 'I':
      if (p.at(1) != 'p') {
        interlaced_ = true;
      }
      break;
    default:
      break;
    }
  }

  /* validate width, height and frame rate */
  if (width_ < 0) {
    throw runtime_error(y4m_path + " : no frame width found");
  }

  if (height_ < 0) {
    throw runtime_error(y4m_path + " : no frame height found");
  }

  if (frame_rate_numerator_ < 0 or frame_rate_denominator_ < 0) {
    throw runtime_error(y4m_path + " : no frame rate found");
  }
}
