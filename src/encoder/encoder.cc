#include <iostream>

#include "exception.hh"
#include "system_runner.hh"

using namespace std;

void usage_error(const string & program_name)
{
  cerr << "Usage: " << program_name << " <video.ts>" << endl;
}

int main(int argc, char * argv[])
{
  if (argc != 2) {
    usage_error(argv[0]);
    return EXIT_FAILURE;
  }

  string video_ts = argv[1];

  vector<string> args = {
    "ffmpeg", "-i", video_ts,
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p",
    "-s", "1920x1080", "-maxrate", "6000k", "-bufsize", "12000k", "-c:a", "libfdk_aac", "1080p.mp4",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p",
    "-s", "1280x720", "-maxrate", "4000k", "-bufsize", "8000k", "-c:a", "libfdk_aac", "720p.mp4",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p",
    "-s", "854x480", "-maxrate", "2000k", "-bufsize", "4000k", "-c:a", "libfdk_aac", "480p.mp4",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p",
    "-s", "640x360", "-maxrate", "1000k", "-bufsize", "2000k", "-c:a", "libfdk_aac", "360p.mp4",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p",
    "-s", "426x240", "-maxrate", "700k", "-bufsize", "1400k", "-c:a", "libfdk_aac", "240p.mp4",
  };

  try {
    run("ffmpeg", args, {}, true, true);
  } catch (const exception & e) {
    print_exception(argv[0], e);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
