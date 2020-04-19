#include <iostream>
#include <string>
#include <fstream>
#include <map>
#include <ctime>

#include "util.hh"
#include "yaml.hh"
#include "media_formats.hh"
#include "exception.hh"
#include "timerfd.hh"
#include "poller.hh"
#include "inotify.hh"
#include "filesystem.hh"
#include "timestamp.hh"
#include "tokenize.hh"
#include "influxdb_client.hh"

using namespace std;
using namespace PollerShortNames;

static const int TIMER_PERIOD_MS = 60000;  /* 1 minute */
static fs::path media_dir;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <YAML configuration>" << endl;
}

void report_decoder_info(const string & channel_name,
                         Inotify & inotify,
                         InfluxDBClient & influxdb_client)
{
  fs::path channel_path = media_dir / channel_name;
  string video_raw = channel_path / "working/video-raw";

  inotify.add_watch(video_raw, IN_MOVED_TO,
    [channel_name, video_raw, &influxdb_client]
    (const inotify_event & event, const string & path) {
      /* only interested in regular files that are moved into the dir */
      if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
        return;
      }

      assert(video_raw == path);
      assert(event.len != 0);

      fs::path filepath = fs::path(path) / event.name;
      if (filepath.extension() == ".info") {
        ifstream decoder_info_stream(filepath);
        string line;
        getline(decoder_info_stream, line);
        vector<string> sp = split(line, " ");

        string log_line = "decoder_info,channel=" + channel_name
          + " timestamp=" + sp[1] + "i,due=" + sp[2] + "i,filler_fields="
          + sp[3] + "i " + sp[0];
        influxdb_client.post(log_line);

        /* remove .y4m.info files after posting to InfluxDB */
        fs::remove(filepath);
      }
    }
  );
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

        string log_line = "ssim,channel=" + channel_name + ",format="
          + vformat + " timestamp=" + ts + "i,ssim_index=" + line
          + " " + to_string(timestamp_ms());
        influxdb_client.post(log_line);
      }
    }
  );
}

void report_video_size(const string & channel_name,
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

        string log_line = "video_size,channel=" + channel_name + ",format="
          + vformat + " timestamp=" + ts + "i,size=" + to_string(filesize)
          + "i " + to_string(timestamp_ms());
        influxdb_client.post(log_line);
      }
    }
  );
}

void report_backlog(const set<string> & channel_set,
                    Poller & poller,
                    Timerfd & timer,
                    InfluxDBClient & influxdb_client)
{
  poller.add_action(Poller::Action(timer, Direction::In,
    [&channel_set, &timer, &influxdb_client]() {
      /* must read the timerfd, and check if timer has fired */
      if (timer.expirations() == 0) {
        return ResultType::Continue;
      }

      for (const auto & channel_name : channel_set) {
        fs::path channel_path = media_dir / channel_name;
        string working_dir = channel_path / "working";
        string canonical_dir = channel_path / "working/video-canonical";

        unsigned int working_cnt = 0;
        for (const auto & entry : fs::recursive_directory_iterator(working_dir)) {
          if (fs::is_regular_file(entry)) {
            working_cnt++;
          }
        }

        unsigned int canonical_cnt = 0;
        for (const auto & entry : fs::recursive_directory_iterator(canonical_dir)) {
          if (fs::is_regular_file(entry)) {
            canonical_cnt++;
          }
        }

        string log_line = "backlog,channel=" + channel_name
          + " working_cnt=" + to_string(working_cnt)
          + "i,canonical_cnt=" + to_string(canonical_cnt)
          + "i " + to_string(timestamp_ms());
        influxdb_client.post(log_line);
      }

      return ResultType::Continue;
    }
  ));
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
  if (not config["enable_logging"].as<bool>()) {
    cerr << "Error: logging is not enabled yet" << endl;
    return EXIT_FAILURE;
  }

  media_dir = config["media_dir"].as<string>();
  set<string> channel_set = load_channels(config);

  Poller poller;
  Inotify inotify(poller);

  const auto & influx = config["influxdb_connection"];
  InfluxDBClient influxdb_client(
      poller,
      {influx["host"].as<string>(), to_string(influx["port"].as<uint16_t>())},
      influx["dbname"].as<string>(),
      influx["user"].as<string>(),
      safe_getenv(influx["password"].as<string>()));

  for (const auto & channel_name : channel_set) {
    const auto & channel_config = config["channel_configs"][channel_name];

    /* report .y4m.info files */
    report_decoder_info(channel_name, inotify, influxdb_client);

    vector<VideoFormat> vformats = channel_video_formats(channel_config);

    for (const auto & vformat : vformats) {
      /* report SSIM indices */
      report_ssim(channel_name, vformat.to_string(),
                  inotify, influxdb_client);

      /* report video sizes */
      report_video_size(channel_name, vformat.to_string(),
                        inotify, influxdb_client);
    }
  }

  /* create a periodic timer that fires every minute to report backlog sizes */
  Timerfd timer;
  report_backlog(channel_set, poller, timer, influxdb_client);
  timer.start(TIMER_PERIOD_MS, TIMER_PERIOD_MS);

  for (;;) {
    auto ret = poller.poll(-1);
    if (ret.result != Poller::Result::Type::Success) {
      return ret.exit_status;
    }
  }

  return EXIT_SUCCESS;
}
