#include "media_formats.hh"

#include <string>
#include <vector>

using namespace std;

/* construct from string "<width>x<height>-<CRF>" */
VideoFormat::VideoFormat(const string & str)
{
  auto x_pos = str.find('x');
  if (x_pos == string::npos) {
    throw runtime_error("invalid video format string: " + str);
  }

  auto dash_pos = str.find('-', x_pos + 1);
  if (dash_pos == string::npos) {
    throw runtime_error("invalid video format string: " + str);
  }

  width = stoi(str.substr(0, x_pos));
  height = stoi(str.substr(x_pos + 1, dash_pos - (x_pos + 1)));
  crf = stoi(str.substr(dash_pos + 1));
}

string VideoFormat::resolution() const
{
  return ::to_string(width) + "x" + ::to_string(height);
}

string VideoFormat::to_string() const
{
  return ::to_string(width) + "x" + ::to_string(height) + "-" + ::to_string(crf);
}

bool VideoFormat::operator<(const VideoFormat & o) const
{
  return tie(width, height, crf) < tie(o.width, o.height, o.crf);
}

bool VideoFormat::operator==(const VideoFormat & o) const
{
  return tie(width, height, crf) == tie(o.width, o.height, o.crf);
}

bool VideoFormat::operator!=(const VideoFormat & o) const
{
  return tie(width, height, crf) != tie(o.width, o.height, o.crf);
}

ostream &operator<<(ostream & os, const VideoFormat & o)
{
  return os << o.to_string();
}

/* construct from string "<bitrate>k" */
AudioFormat::AudioFormat(const string & str)
{
  auto pos = str.find('k');
  if (pos == string::npos) {
    throw runtime_error("invalid audio format: " + str);
  }

  bitrate = stoi(str.substr(0, pos));
}

string AudioFormat::to_string() const
{
  return ::to_string(bitrate) + "k";
}

bool AudioFormat::operator<(const AudioFormat & o) const
{
  return bitrate < o.bitrate;
}

bool AudioFormat::operator==(const AudioFormat & o) const
{
  return bitrate == o.bitrate;
}

bool AudioFormat::operator!=(const AudioFormat & o) const
{
  return bitrate != o.bitrate;
}

ostream &operator<<(ostream & os, const AudioFormat & o)
{
  return os << o.to_string();
}

vector<VideoFormat> get_video_formats(const YAML::Node & config)
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

vector<AudioFormat> get_audio_formats(const YAML::Node & config)
{
  vector<AudioFormat> aformats;

  const YAML::Node & bitrate_list = config["audio"];
  for (const auto & bitrate_node : bitrate_list) {
    const string & aformat_str = bitrate_node.as<string>();
    aformats.emplace_back(aformat_str);
  }

  return aformats;
}
