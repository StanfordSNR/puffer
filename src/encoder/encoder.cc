#include <iostream>
#include <string>
#include <vector>
#include <map>

#include "exception.hh"
#include "system_runner.hh"
#include "tokenize.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <input_segment.ts>" << endl
       << endl
       << "<input_segment.ts>    MPEG transport stream"
       << endl;
}

string strip_file_extension(const string & full_filename)
{
  auto pos = full_filename.find_last_of('.');

  if (pos == string::npos or full_filename.substr(pos) != ".ts") {
    throw runtime_error("Input segment should be an MPEG-TS file");
  }

  return full_filename.substr(0, pos);
}

void decode_video(const string & input_ts, const string & video_y4m)
{
  /* deinterlace if need to */
  string args_str = "ffmpeg -y -i " + input_ts + " -vf yadif " + video_y4m;
  cerr << "$ " << args_str << endl;

  vector<string> args = split(args_str, " ");
  run("ffmpeg", args, {}, true, true);
}

void encode_audio(const string & input_ts, const string & audio_aac)
{
  string args_str = "ffmpeg -y -i " + input_ts +
                    " -c:a libfdk_aac -b:a 128k -vn " + audio_aac;
  cerr << "$ " << args_str << endl;

  vector<string> args = split(args_str, " ");
  run("ffmpeg", args, {}, true, true);
}

void encode_video(const string & video_y4m,
                  const map<string, vector<string>> & video_config)
{
  string common_args = " -c:v libx264 -crf 23 -preset veryfast -an";

  string args_str = "ffmpeg -y -i " + video_y4m;
  for (const auto & kv : video_config) {
    const auto & v = kv.second;
    args_str += common_args + " -s " + v[0] + " -maxrate " + v[1] +
                " -bufsize " + v[2] + " " + kv.first;
  }
  cerr << "$ " << args_str << endl;

  vector<string> args = split(args_str, " ");
  run("ffmpeg", args, {}, true, true);
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string input_ts{argv[1]};
  string filename = strip_file_extension(input_ts);

  /* extract video from TS and decode into Y4M */
  string video_y4m = filename + ".y4m";
  decode_video(input_ts, video_y4m);

  /* extract audio from TS and encode into AAC in MP4 */
  string audio_aac = filename + "-audio.mp4";
  encode_audio(input_ts, audio_aac);

  /* encode Y4M into different qualities */
  map<string, vector<string>> video_config = {
    /* {output_video,   {resolution,  maxrate, bufsize}} */
    {filename + "-1080p.mp4", {"1920x1080", "6000k", "12000k"}},
    {filename + "-720p.mp4",  {"1280x720",  "4000k", "8000k"}},
    {filename + "-480p.mp4",  {"854x480",   "2000k", "4000k"}},
    {filename + "-360p.mp4",  {"640x360",   "1000k", "2000k"}},
    {filename + "-240p.mp4",  {"426x240",   "700k",  "1400k"}},
  };
  encode_video(video_y4m, video_config);

  return EXIT_SUCCESS;
}
