#include <sys/stat.h>
#include <iostream>
#include <string>
#include <regex>
#include <vector>
#include <system_error>

#include "filesystem.hh"
#include "exception.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <shmdir> <output>\n\n"
  "<shmdir>   directory containing raw videos and audio for a channel\n\n"
  "<output>   directory containing downstream files\n\n"
  << endl;
}

void clean_raw_video_files(const string & video_dir, const string & output_dir) {
  /* Obtain the video qualities */
  vector<string> ssim_dirs;
  basic_regex ssim_dir_regex("\\d+x\\d+-\\d+-mp4-ssim");
  for (const auto & entry : fs::directory_iterator(output_dir)) {
    string path = entry.path().string();
    string basename = basename.substr(basename.find_last_of("/\\") + 1);
    if (fs::is_directory(path) && regex_match(basename, ssim_dir_regex)) {
      ssim_dirs.push_back(path);
    }
  }

  /* List the raw video files */
  basic_regex raw_video_regex("(\\d+).y4m");
  for (const auto & entry : fs::directory_iterator(video_dir)) {
    string raw_video_path = entry.path().string();
    string raw_video_basename = raw_video_path.substr(raw_video_path.find_last_of("/\\") + 1);
    std::smatch match;
    if (fs::is_regular_file(raw_video_path) &&
        regex_match(raw_video_basename , match, raw_video_regex) &&
        match.size() == 2) {
      string timestamp = match[1].str();

      /* Remove the video file if all ssims are done */
      bool ssim_done = true;
      for (string & ssim_dir : ssim_dirs) {
        if (!fs::exists(ssim_dir + "/" + timestamp + ".ssim")) {
          ssim_done = false;
          break;
        }
      }
      if (ssim_done) {
        error_code ec;
        fs::remove(raw_video_path, ec);
      }
    }
  }
}

void clean_raw_audio_files(const string & audio_dir, const string & output_dir) {
  /* Obtain the audio qualities */
  vector<string> audio_output_dirs;
  basic_regex audio_dir_regex("\\d+k");
  for (const auto & entry : fs::directory_iterator(output_dir)) {
    string path = entry.path().string();
    string basename = basename.substr(basename.find_last_of("/\\") + 1);
    if (fs::is_directory(path) && regex_match(path, audio_dir_regex)) {
      audio_output_dirs.push_back(path);
    }
  }

  /* List the raw audio files */
  basic_regex raw_audio_regex("(\\d+).wav");
  for (const auto & entry : fs::directory_iterator(audio_dir)) {
    string raw_audio_path = entry.path().string();
    string raw_audio_basename = raw_audio_path.substr(raw_audio_path.find_last_of("/\\") + 1);
    std::smatch match;
    if (fs::is_regular_file(raw_audio_path) &&
        regex_match(raw_audio_basename, match, raw_audio_regex) &&
        match.size() == 2) {
      string timestamp = match[1].str();

      /* Remove the audio file if all ssims are done */
      bool done_encoding = true;
      for (string & dir : audio_output_dirs) {
        if (!fs::exists(dir + "/" + timestamp + ".chk")) {
          done_encoding = false;
          break;
        }
      }
      if (done_encoding) {
        error_code ec;
        fs::remove(raw_audio_path, ec);
      }
    }
  }
}

int main(int argc, char * argv[])
{
  if (argc < 3) {
    abort();
  }

  string shm_dir, output_dir;
  shm_dir = argv[0];
  output_dir = argv[1];

  clean_raw_video_files(shm_dir + "/raw-video", output_dir);
  clean_raw_audio_files(output_dir + "/raw-audio", output_dir);

  return EXIT_SUCCESS;
}
