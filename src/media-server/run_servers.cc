#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <crypto++/sha.h>
#include <crypto++/hex.h>
#include <pqxx/pqxx>

#include "filesystem.hh"
#include "path.hh"
#include "child_process.hh"
#include "system_runner.hh"
#include "util.hh"
#include "exception.hh"
#include "timestamp.hh"
#include "yaml.hh"

using namespace std;
using namespace CryptoPP;

static string yaml_config;
static YAML::Node config;
static fs::path src_path;  /* path to puffer/src directory */

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <YAML configuration>" << endl;
}

string sha256(const string & input)
{
  SHA256 hash;
  string digest;
  StringSource s(input, true,
    new HashFilter(hash,
      new HexEncoder(
        new StringSink(digest))));
  return digest;
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

  /* save as global variables */
  yaml_config = fs::absolute(argv[1]);
  config = YAML::LoadFile(yaml_config);
  src_path = fs::canonical(fs::path(
      roost::readlink("/proc/self/exe")).parent_path().parent_path());

  ProcessManager proc_manager;

  const bool enable_logging = config["enable_logging"].as<bool>();
  if (enable_logging) {
    cerr << "Logging is enabled" << endl;
  } else {
    cerr << "Logging is disabled" << endl;
  }

  /* will run log reporters only if enable_logging is true */
  auto log_reporter = src_path / "monitoring/log_reporter";
  vector<string> log_stems {
    "server_info", "active_streams", "client_buffer", "client_sysinfo",
    "video_sent", "video_acked"};

  /* Remove ipc directory prior to starting Media Server */
  string ipc_dir = "pensieve_ipc";
  if (fs::exists(ipc_dir)) {
    fs::remove_all(ipc_dir);
  }

  /* run media servers in each experimental group */
  const auto & expt_json = src_path / "scripts" / "expt_json.py";
  const auto & ws_media_server = src_path / "media-server/ws_media_server";

  int server_id = 0;
  for (const auto & expt : config["experiments"]) {
    /* convert YAML::Node to string */
    stringstream ss;
    ss << expt["fingerprint"];
    string fingerprint = ss.str();

    /* run expt_json.py to represent experimental settings as a JSON string */
    string json_str = run(expt_json, {expt_json, fingerprint}, true).first;

    string expt_id = expt["fingerprint"]["abr"].as<string>() + "+" +
                     expt["fingerprint"]["cc"].as<string>();
    if (expt["fingerprint"]["abr_name"]) {
      expt_id = expt["fingerprint"]["abr_name"].as<string>() + "+" +
                expt["fingerprint"]["cc"].as<string>();

    }
    unsigned int num_servers = expt["num_servers"].as<unsigned int>();

    cerr << "Running experiment " << expt_id << " on "
         << num_servers << " servers" << endl;

    for (unsigned int i = 0; i < num_servers; i++) {
      server_id++;

      /* run ws_media_server */
      vector<string> args { ws_media_server, yaml_config,
                            to_string(server_id), expt_id };
      proc_manager.run_as_child(ws_media_server, args);

      /* run log_reporter */
      if (enable_logging) {
        fs::path log_dir = config["log_dir"].as<string>();

        for (const auto & log_stem : log_stems) {
          string log_format = log_dir / (log_stem + ".conf");
          string log_path = log_dir / (log_stem + "." + to_string(server_id)
                                       + ".log");

          vector<string> log_args { log_reporter, yaml_config,
                                    log_format, log_path };
          proc_manager.run_as_child(log_reporter, log_args);
        }
      }
    }
  }

  return proc_manager.wait();
}
