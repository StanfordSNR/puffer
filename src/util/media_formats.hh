#ifndef MEDIA_FORMATS_HH
#define MEDIA_FORMATS_HH

#include <string>
#include <iostream>

struct VideoFormat
{
  VideoFormat(const std::string & str);

  int width {};
  int height {};
  int crf {};

  std::string resolution() const;
  std::string to_string() const;

  bool operator<(const VideoFormat & o) const;
  bool operator==(const VideoFormat & o) const;
  bool operator!=(const VideoFormat & o) const;
};

std::ostream &operator<<(std::ostream & os, const VideoFormat & o);

struct AudioFormat
{
  AudioFormat(const std::string & str);

  int bitrate {};  /* kbps */

  std::string to_string() const;

  bool operator<(const AudioFormat & o) const;
  bool operator==(const AudioFormat & o) const;
  bool operator!=(const AudioFormat & o) const;
};

std::ostream &operator<<(std::ostream & os, const AudioFormat & o);

#endif /* MEDIA_FORMATS_HH */
