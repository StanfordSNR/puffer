#include <iostream>
#include <string>
#include <fstream>
#include <map>
#include <ctime>

#include "util.hh"
#include "yaml.hh"
#include "media_formats.hh"
#include "exception.hh"
#include "poller.hh"
#include "inotify.hh"
#include "filesystem.hh"
#include "influxdb_client.hh"

using namespace std;

fs::path media_dir;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <YAML configuration>" << endl;
}

void report_ssim(const string & channel_name,
                 const string & vformat,
                 Inotify & inotify,
                 InfluxDBClient & influxdb_client)
{
  fs::path channel_path = media_dir / channel_name;
  string ssim_dir = channel_path / "ready" / (vformat + "-ssim");

  inotify.add_watch(ssim_dir, IN_MOVED_TO,
    [channel_name, vformat, ssim_dir, &influxdb_client]
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
          + vformat + " timestamp=" + ts + "i,ssim=" + line
          + " " + to_string(time(nullptr));
        influxdb_client.post(log_line);
      }
    }
  );
}

void report_size(const string & channel_name,
                 const string & vformat,
                 Inotify & inotify,
                 InfluxDBClient & influxdb_client)
{
  fs::path channel_path = media_dir / channel_name;
  string video_dir = channel_path / "ready" / vformat;

  inotify.add_watch(video_dir, IN_MOVED_TO,
    [channel_name, vformat, video_dir, &influxdb_client]
    (const inotify_event & event, const string & path) {
      /* only interested in regular files that are moved into the dir */
      if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
        return;
      }

      assert(video_dir == path);
      assert(event.len != 0);

      fs::path filepath = fs::path(path) / event.name;
      if (filepath.extension() == ".m4s") {
        string ts = filepath.stem();
        const auto filesize = fs::file_size(filepath);

        string log_line = "video_size,channel=" + channel_name + ",quality="
          + vformat + " timestamp=" + ts + "i,size=" + to_string(filesize)
          + "i " + to_string(time(nullptr));
        influxdb_client.post(log_line);
      }
    }
  );
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

  YAML::Node config = YAML::LoadFile(argv[1]);
  media_dir = config["media_dir"].as<string>();
  set<string> channel_set = load_channels(config);

  Poller poller;
  Inotify inotify(poller);
  InfluxDBClient influxdb_client(poller, {"127.0.0.1", 8086}, "collectd",
                                 "puffer", safe_getenv("INFLUXDB_PASSWORD"));

  for (const auto & channel_name : channel_set) {
    const auto & channel_config = config["channel_configs"][channel_name];
    vector<VideoFormat> vformats = channel_video_formats(channel_config);

    for (const auto & vformat : vformats) {
      /* report SSIM indices */
      report_ssim(channel_name, vformat.to_string(), inotify, influxdb_client);

      /* report video sizes */
      report_size(channel_name, vformat.to_string(), inotify, influxdb_client);
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
