#include "y4m.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "exception.hh"
#include "tokenize.hh"

using namespace std;

Y4MParser::Y4MParser(const string & y4m_path)
  : width_(-1), height_(-1), frame_rate_numerator_(-1),
    frame_rate_denominator_(-1), interlaced_(false)
{
  /* only the first line is needed so getline() is more efficient */
  FILE *fp = fopen(y4m_path.c_str(), "r");
  if (fp == NULL) {
    throw runtime_error("fopen failed to open " + y4m_path);
  }

  char * line_ptr = NULL;
  size_t len = 0;
  if (getline(&line_ptr, &len, fp) == -1) {
    free(line_ptr);
    throw runtime_error("getline failed to read a line from " + y4m_path);
  }

  string line(line_ptr);
  free(line_ptr);

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
      width_ = stol(p.substr(1));
      break;
    case 'H':
      height_ = stol(p.substr(1));
      break;
    case 'F':
      pos = p.find(':');
      if (pos != string::npos) {
        frame_rate_numerator_ = stol(p.substr(1, pos - 1));
        frame_rate_denominator_ = stol(p.substr(pos + 1));
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
