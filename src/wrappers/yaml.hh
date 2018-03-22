#ifndef YAML_HH
#define YAML_HH

#include <string>
#include <iostream>
#include "yaml-cpp/yaml.h"

struct VideoFormat
{
  int width;
  int height;
  int crf;

  std::string resolution() const;

  std::string to_string() const;
  bool operator<(const VideoFormat & o) const;
  bool operator==(const VideoFormat & o) const;
  bool operator!=(const VideoFormat & o) const;
};

std::ostream &operator<<(std::ostream & os, const VideoFormat & o);

struct AudioFormat
{
  int bitrate;

  std::string to_string() const;
  bool operator<(const AudioFormat & o) const;
  bool operator==(const AudioFormat & o) const;
  bool operator!=(const AudioFormat & o) const;
};

std::ostream &operator<<(std::ostream & os, const AudioFormat & o);

/* load and validate the YAML file */
YAML::Node load_yaml(const std::string & yaml_path);

/* get video formats (resolution, CRF) from YAML configuration */
std::vector<VideoFormat> get_video_formats(const YAML::Node & config);

/* get audio formats (bitrate) from YAML configuration */
std::vector<AudioFormat> get_audio_formats(const YAML::Node & config);

#endif /* YAML_HH */
