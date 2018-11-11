#include "yaml.hh"

using namespace std;

vector<VideoFormat> channel_video_formats(const YAML::Node & config)
{
  vector<VideoFormat> vformats;

  const YAML::Node & res_map = config["video"];
  for (const auto & res_node : res_map) {
    const string & res = res_node.first.as<string>();

    const YAML::Node & crf_list = res_node.second;
    for (const auto & crf_node : crf_list) {
      const auto & vformat_str = res + "-" + crf_node.as<string>();
      vformats.emplace_back(vformat_str);
    }
  }

  return vformats;
}

vector<AudioFormat> channel_audio_formats(const YAML::Node & config)
{
  vector<AudioFormat> aformats;

  const YAML::Node & bitrate_list = config["audio"];
  for (const auto & bitrate_node : bitrate_list) {
    const string & aformat_str = bitrate_node.as<string>();
    aformats.emplace_back(aformat_str);
  }

  return aformats;
}
