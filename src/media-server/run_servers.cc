#include <iostream>
#include <string>
#include <memory>

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

  string yaml_config = fs::absolute(argv[1]);
  YAML::Node config = YAML::LoadFile(yaml_config);

  int num_servers = stoi(argv[2]);
  if (num_servers <= 0) {
    cerr << "<number of servers>: a positive integer is required" << endl;
    return EXIT_FAILURE;
  }

  const bool enable_logging = config["enable_logging"].as<bool>();
  const auto & src_path = fs::canonical(fs::path(
      roost::readlink("/proc/self/exe")).parent_path().parent_path());

  ProcessManager proc_manager;

  /* create influxdb_client only if enable_logging is true */
  unique_ptr<InfluxDBClient> influxdb_client = nullptr;
  if (enable_logging) {
    /* add influx client for posting states of child processes */
    const auto & influx = config["influxdb_connection"];
    influxdb_client = make_unique<InfluxDBClient>(
        proc_manager.poller(),
        Address(influx["host"].as<string>(), influx["port"].as<uint16_t>()),
        influx["dbname"].as<string>(),
        influx["user"].as<string>(),
        safe_getenv(influx["password"].as<string>()));
  }

  /* run multiple instances of ws_media_server */
  const auto & ws_media_server = src_path / "media-server/ws_media_server";
  for (int i = 0; i < num_servers; i++) {
    vector<string> args { ws_media_server, yaml_config, to_string(i) };
    proc_manager.run_as_child(ws_media_server, args, {},
      [&influxdb_client, i](const pid_t &)  // error callback
      {
        cerr << "Error in media server with ID " << i << endl;
        if (influxdb_client) {
          /* at least one media server have failed */
          influxdb_client->post("server_state state=1i "
                                + to_string(time(nullptr)));
        }
      }
    );
  }

  /* more logging actions */
  if (enable_logging and influxdb_client) {
    /* indicate that media servers are running */
    influxdb_client->post("server_state state=0i "
                          + to_string(time(nullptr)));

    /* run log reporters */
    auto log_reporter = src_path / "monitoring/log_reporter";
    fs::path log_dir = config["log_dir"].as<string>();
    vector<string> log_stems {
      "active_streams", "playback_buffer", "rebuffer_event", "rebuffer_rate",
      "video_quality"};

    /* run a log reporter for each ws_media_server instance */
    for (int i = 0; i < num_servers; i++) {
      for (const auto & log_stem : log_stems) {
        string log_format = log_dir / (log_stem + ".conf");
        string log_path = log_dir / (log_stem + "." + to_string(i) + ".log");

        vector<string> log_args { log_reporter, yaml_config,
                                  log_format, log_path };
        proc_manager.run_as_child(log_reporter, log_args, {},
          [&influxdb_client, log_stem](const pid_t &)  // error callback
          {
            cerr << "Error in log reporter: " << log_stem << endl;
            /* at least one log reporter have failed */
            influxdb_client->post("log_reporter_state state=1i "
                                  + to_string(time(nullptr)));
          }
        );
      }
    }

    /* indicate that log reporters are running */
    influxdb_client->post("log_reporter_state state=0i "
                          + to_string(time(nullptr)));
  }

  return proc_manager.wait();
}
