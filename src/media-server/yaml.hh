#ifndef YAML_HH
#define YAML_HH

#include <string>
#include <map>
#include <set>
#include <vector>

#include "yaml-cpp/yaml.h"
#include "media_formats.hh"

/* get all channel names */
std::set<std::string> load_channels(const YAML::Node & config);

/* get video formats of a specific channel's config */
std::vector<VideoFormat> channel_video_formats(const YAML::Node & channel_config);

/* get audio formats of a specific channel's config */
std::vector<AudioFormat> channel_audio_formats(const YAML::Node & channel_config);

/* get connection string of postgres_connection */
std::string postgres_connection_string(const YAML::Node & postgres_connection_config);

#endif /* YAML_HH */
