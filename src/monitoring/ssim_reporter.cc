#include <iostream>
#include <string>
#include <fstream>
#include <map>
#include <ctime>

#include "util.hh"
#include "exception.hh"
#include "poller.hh"
#include "inotify.hh"
#include "filesystem.hh"
#include "influxdb_client.hh"

using namespace std;

/* channel name -> (video format -> ssim path) */
static map<string, map<string, string>> dirmap;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <target directory>" << endl;
}

void parse_target_directory(const string & target_dir)
{
  for (const auto & channel_dir : fs::directory_iterator(target_dir)) {
    if (not fs::is_directory(channel_dir)) {
      continue;
    }

    string channel_name = channel_dir.path().filename();

    const auto & ready_path = channel_dir.path() / "ready";
    for (const auto & ssim_dir : fs::directory_iterator(ready_path)) {
      if (not fs::is_directory(ssim_dir)) {
        continue;
      }

      string ssim_dir_name = ssim_dir.path().filename();
      if (ssim_dir_name.size() >= 5 and /* at least ending with "-ssim" */
          ssim_dir_name.substr(ssim_dir_name.size() - 4) == "ssim") {
        string video_format = ssim_dir_name.substr(0, ssim_dir_name.size() - 5);

        dirmap[channel_name][video_format] = ssim_dir.path();
      }
    }
  }
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

  parse_target_directory(argv[1]);

  Poller poller;
  Inotify inotify(poller);
  InfluxDBClient influxdb_client(poller, {"127.0.0.1", 8086}, "collectd",
                                 "puffer", safe_getenv("INFLUXDB_PASSWORD"));

  for (const auto & channel_name_pair : dirmap) {
    for (const auto & video_format_pair: channel_name_pair.second) {
      string channel_name = channel_name_pair.first;
      string video_format = video_format_pair.first;
      string ssim_dir = video_format_pair.second;

      inotify.add_watch(ssim_dir, IN_MOVED_TO,
        [channel_name, video_format, ssim_dir, &influxdb_client]
        (const inotify_event & event, const string & path) {
          /* only interested in regular files that are moved into the dir */
          if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
            return;
          }

          assert(ssim_dir == path);
          assert(event.len != 0);

          fs::path filepath = fs::path(path) / event.name;
          if (filepath.extension() == ".ssim") {
            string ts = filepath.stem();

            ifstream ssim_file(filepath);
            string line;
            getline(ssim_file, line);

            /* make sure SSIM is valid */
            try {
              stod(line);
            } catch (const exception & e) {
              print_exception("invalid SSIM file", e);
              return;
            }

            string log_line = "ssim,channel=" + channel_name + ",quality="
              + video_format + " timestamp=" + ts + "i,ssim=" + line
              + " " + to_string(time(nullptr));
            influxdb_client.post(log_line);
          }
        }
      );
    }
  }

  for (;;) {
    auto ret = poller.poll(-1);
    if (ret.result != Poller::Result::Type::Success) {
      return ret.exit_status;
    }
  }

  return EXIT_SUCCESS;
}
