#include "yaml.hh"
#include "util.hh"

using namespace std;

set<string> load_channels(const YAML::Node & config)
{
  set<string> channel_set;

  for (YAML::const_iterator it = config["channels"].begin();
       it != config["channels"].end(); ++it) {
    const string & channel_name = it->as<string>();

    if (not config["channel_configs"][channel_name]) {
      throw runtime_error("Cannot find details of channel: " + channel_name);
    }

    if (not channel_set.emplace(channel_name).second) {
      throw runtime_error("Found duplicate channel: " + channel_name);
    }
  }

  return channel_set;
}

vector<VideoFormat> channel_video_formats(const YAML::Node & channel_config)
{
  set<VideoFormat> vformats;

  const auto & res_map = channel_config["video"];
  for (const auto & res_node : res_map) {
    const string & res = res_node.first.as<string>();

    const auto & crf_list = res_node.second;
    for (const auto & crf_node : crf_list) {
      const auto & vformat_str = res + "-" + crf_node.as<string>();
      if (not vformats.emplace(vformat_str).second) {
        throw runtime_error("Duplicate video format " + vformat_str);
      }
    }
  }

  return { vformats.begin(), vformats.end() };
}

vector<AudioFormat> channel_audio_formats(const YAML::Node & channel_config)
{
  set<AudioFormat> aformats;

  const auto & bitrate_list = channel_config["audio"];
  for (const auto & bitrate_node : bitrate_list) {
    const string & aformat_str = bitrate_node.as<string>();
    if (not aformats.emplace(aformat_str).second) {
      throw runtime_error("Duplicate audio format " + aformat_str);
    }
  }

  return { aformats.begin(), aformats.end() };
}

string postgres_connection_string(const YAML::Node & config)
{
  string ret;

  ret = "host=" + config["host"].as<string>();
  ret += " port=" + to_string(config["port"].as<uint16_t>());
  ret += " dbname=" + config["dbname"].as<string>();
  ret += " user=" + config["user"].as<string>();
  ret += " password=" + safe_getenv(config["password"].as<string>());

  if (config["sslmode"]) {
    ret += " sslmode=" + config["sslmode"].as<string>();
    ret += " sslrootcert=" + config["sslrootcert"].as<string>();
    ret += " sslcert=" + config["sslcert"].as<string>();
    ret += " sslkey=" + config["sslkey"].as<string>();
  }

  return ret;
}
