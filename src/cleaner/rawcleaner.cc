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
  "Usage: " << program_name << " <video> <audio> <output>\n\n"
  "<video>      directory containing raw video\n"
  "<audio>      directory containing raw audio\n"
  "<output>     directory containing downstream files"
  << endl;
}

vector<string> list_ssim_dirs(const string & output_dir) {
  vector<string> ssim_dirs;
  basic_regex ssim_dir_regex("\\d+x\\d+-\\d+-mp4-ssim");
  for (const auto & entry : fs::directory_iterator(output_dir)) {
    string path = entry.path().string();
    string basename = path.substr(path.find_last_of("/") + 1);
    if (fs::is_directory(path) && regex_match(basename, ssim_dir_regex)) {
      ssim_dirs.push_back(path);
    }
  }
  return ssim_dirs;
}

void clean_raw_video_files(const string & video_dir, const string & output_dir) {
  /* Obtain the video qualities */
  vector<string> ssim_dirs = list_ssim_dirs(output_dir);
  if (ssim_dirs.size() == 0) {
    cerr << "Error: no ssim directories found" << endl;
    abort();
  } else {
    for (string & dir : ssim_dirs) {
      cout << "Expecting ssim in " << dir << endl;
    }
  }

  /* List the raw video files */
  basic_regex raw_video_regex("(\\d+).y4m");
  for (const auto & entry : fs::directory_iterator(video_dir)) {
    string raw_video_path = entry.path().string();
    string raw_video_basename = raw_video_path.substr(raw_video_path.find_last_of("/") + 1);
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
        cout << "Removing " << raw_video_path << endl;
        if (!fs::remove(raw_video_path)) {
          cerr << "Error: failed to remove " << raw_video_path << endl;
        }
      } else {
        cout << "Keeping " << raw_video_path << endl;
      }
    }
  }
}

vector<string> list_audio_output_dirs(const string & output_dir) {
  vector<string> audio_output_dirs;
  basic_regex audio_dir_regex("\\d+k");
  for (const auto & entry : fs::directory_iterator(output_dir)) {
    string path = entry.path().string();
    string basename = path.substr(path.find_last_of("/") + 1);
    if (fs::is_directory(path) && regex_match(basename, audio_dir_regex)) {
      audio_output_dirs.push_back(path);
    }
  }
  return audio_output_dirs;
}

void clean_raw_audio_files(const string & audio_dir, const string & output_dir) {
  /* Obtain the audio qualities */
  vector<string> audio_output_dirs = list_audio_output_dirs(output_dir);
  if (audio_output_dirs.size() == 0) {
    cerr << "No audio output directories found" << endl;
    abort();
  } else {
    for (string & dir : audio_output_dirs) {
      cout << "Expecting audio chunks in " << dir << endl;
    }
  }

  /* List the raw audio files */
  basic_regex raw_audio_regex("(\\d+).wav");
  for (const auto & entry : fs::directory_iterator(audio_dir)) {
    string raw_audio_path = entry.path().string();
    string raw_audio_basename = raw_audio_path.substr(raw_audio_path.find_last_of("/") + 1);
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
        cout << "Removing " << raw_audio_path << endl;
        if (!fs::remove(raw_audio_path)) {
          cerr << "Error: failed to remove " << raw_audio_path << endl;
        }
      } else {
        cout << "Keeping " << raw_audio_path << endl;
      }
    }
  }
}

int main(int argc, char * argv[])
{
  if (argc < 4) {
    print_usage("rawcleaner");
    return EXIT_FAILURE;
  }

  string video_dir, audio_dir, output_dir;
  video_dir = argv[1];
  audio_dir = argv[2];
  output_dir = argv[3];

  if (!fs::exists(video_dir)) {
    cerr << "Raw video directory does not exist " << video_dir << endl;
    return EXIT_FAILURE;
  }

  if (!fs::exists(audio_dir)) {
    cerr << "Raw audio directory does not exist " << audio_dir << endl;
    return EXIT_FAILURE;
  }

  clean_raw_video_files(video_dir, output_dir);
  clean_raw_audio_files(audio_dir, output_dir);

  return EXIT_SUCCESS;
}
