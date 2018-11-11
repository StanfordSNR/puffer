#ifndef YAML_HH
#define YAML_HH

#include <string>
#include <map>

#include "yaml-cpp/yaml.h"
#include "media_formats.hh"

/* get video formats of a channel from its channel config */
std::vector<VideoFormat> channel_video_formats(const YAML::Node & config);

/* get audio formats of a channel from its channel config */
std::vector<AudioFormat> channel_audio_formats(const YAML::Node & config);

#endif /* YAML_HH */
