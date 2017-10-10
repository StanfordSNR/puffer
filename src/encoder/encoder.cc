#include <iostream>
#include <string>
#include <vector>
#include <map>

#include "exception.hh"
#include "system_runner.hh"
#include "tokenize.hh"

using namespace std;

void usage_error(const string & program_name)
{
  cerr << "Usage: " << program_name << " <video.ts>" << endl;
}

/* encode audio once */
void encode_audio(const string & video_ts, const string & output_audio)
{
  string args_str = "ffmpeg -y -i " + video_ts +
                    " -c:a libfdk_aac -b:a 128k -vn " + output_audio;
  vector<string> args = split(args_str, " ");
  run("ffmpeg", args, {}, true, true);
}

/* encode video into different qualities */
void encode_video(const string & video_ts,
                  const map<string, vector<string>> & video_config)
{
  string common_args =
    " -c:v libx264 -preset veryfast -crf 23 -pix_fmt yuv420p -an"
    " -force_key_frames expr:gte(t,n_forced*2)";

  string args_str = "ffmpeg -y -i " + video_ts;
  for (const auto & kv : video_config) {
    const auto & v = kv.second;
    args_str += common_args + " -s " + v[0] + " -maxrate " + v[1] +
                " -bufsize " + v[2] + " " + kv.first;
  }

  vector<string> args = split(args_str, " ");
  run("ffmpeg", args, {}, true, true);
}

/* mux the audio and different videos */
void mux_audio_video(const string & output_audio,
                     const map<string, vector<string>> & video_config)
{
  for (const auto & kv : video_config) {
    const string & output_video = kv.first;
    string output_av = "audio_" + output_video;
    string args_str = "ffmpeg -y -i " + output_audio + " -i " + output_video +
                      " -c copy -map 0:a:0 -map 1:v:0 -shortest " + output_av;
    vector<string> args = split(args_str, " ");
    run("ffmpeg", args, {}, true, true);
  }
}

int main(int argc, char * argv[])
{
  if (argc != 2) {
    usage_error(argv[0]);
    return EXIT_FAILURE;
  }

  // TODO: add more args, such as paths of output files; add sanity checks

  string video_ts = argv[1];

  string output_audio = "audio.aac";
  encode_audio(video_ts, output_audio);

  map<string, vector<string>> video_config = {
    /* {output_video,   {resolution,  maxrate, bufsize}} */
    {"video_1080p.mp4", {"1920x1080", "6000k", "12000k"}},
    {"video_720p.mp4",  {"1280x720",  "4000k", "8000k"}},
    {"video_480p.mp4",  {"854x480",   "2000k", "4000k"}},
    {"video_360p.mp4",  {"640x360",   "1000k", "2000k"}},
    {"video_240p.mp4",  {"426x240",   "700k",  "1400k"}},
  };
  encode_video(video_ts, video_config);

  mux_audio_video(output_audio, video_config);

  return EXIT_SUCCESS;
}
