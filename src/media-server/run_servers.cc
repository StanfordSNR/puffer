#include <iostream>
#include <string>

#include "yaml-cpp/yaml.h"
#include "filesystem.hh"
#include "path.hh"
#include "child_process.hh"
#include "influxdb_client.hh"
#include "util.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <YAML configuration> <number of servers>"
  << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string yaml_config(argv[1]);
  int num_servers = stoi(argv[2]);

  if (num_servers <= 0) {
    cerr << "<number of servers>: a positive integer is required" << endl;
    return EXIT_FAILURE;
  }

  /* get the paths of ws_media_server and log_reporter */
  auto media_server_dir = fs::canonical(fs::path(
                          roost::readlink("/proc/self/exe")).parent_path());
  auto ws_media_server = media_server_dir / "ws_media_server";
  auto log_reporter = media_server_dir.parent_path() / "monitoring/log_reporter";

  /* load YAML config and get log_dir */
  YAML::Node config = YAML::LoadFile(yaml_config);
  fs::path log_dir = config["log_dir"].as<string>();
  vector<string> log_stems {
    "active_streams", "playback_buffer", "rebuffer_event", "rebuffer_rate",
    "video_quality"};

  ProcessManager proc_manager;

  /* add influx client for posting states of child processes */
  InfluxDBClient influxdb_client(proc_manager.poller(), {"127.0.0.1", 8086},
                                 "collectd", "puffer",
                                 safe_getenv("INFLUXDB_PASSWORD"));

  /* run multiple instances of ws_media_server */
  for (int i = 0; i < num_servers; i++) {
    vector<string> args { ws_media_server, yaml_config, to_string(i) };
    proc_manager.run_as_child(ws_media_server, args, {},
      [&influxdb_client](const pid_t & pid)  // error callback
      {
        cerr << "Error in media server: pid " << to_string(pid) << endl;
        influxdb_client.post("server_state,pid=" + to_string(pid)
                             + " state=\"error\" "
                             + to_string(time(nullptr)));
      }
    );

    for (const auto & log_stem : log_stems) {
      string log_format = log_dir / (log_stem + ".conf");
      string log_path = log_dir / (log_stem + "." + to_string(i) + ".log");

      vector<string> log_args {log_reporter, log_format, log_path};
      proc_manager.run_as_child(log_reporter, log_args, {},
        [&influxdb_client](const pid_t & pid)  // error callback
        {
          cerr << "Error in log reporter: pid " << to_string(pid) << endl;
          influxdb_client.post("log_reporter_state,pid=" + to_string(pid)
                               + " state=\"error\" "
                               + to_string(time(nullptr)));
        }
      );
    }
  }

  return proc_manager.wait();
}
