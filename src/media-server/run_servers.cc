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
  cerr << "Usage: " << program_name << " <YAML configuration> [--maintenance]" << endl;
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

int retrieve_expt_id(const string & json_str)
{
  try {
    /* compute SHA-256 */
    string hash = sha256(json_str);

    /* connect to PostgreSQL */
    string db_conn_str = postgres_connection_string(config["postgres_connection"]);
    pqxx::connection db_conn(db_conn_str);

    /* create table if not exists */
    pqxx::work create_table(db_conn);
    create_table.exec("CREATE TABLE IF NOT EXISTS puffer_experiment "
                      "(id SERIAL PRIMARY KEY,"
                      " hash VARCHAR(64) UNIQUE NOT NULL,"
                      " data jsonb);");
    create_table.commit();

    /* prepare two statements */
    db_conn.prepare("select_id",
      "SELECT id FROM puffer_experiment WHERE hash = $1;");
    db_conn.prepare("insert_json",
      "INSERT INTO puffer_experiment (hash, data) VALUES ($1, $2) RETURNING id;");

    pqxx::work db_work(db_conn);

    /* try to fetch an existing row */
    pqxx::result r = db_work.exec_prepared("select_id", hash);
    if (r.size() == 1 and r[0].size() == 1) {
      /* the same hash already exists */
      return r[0][0].as<int>();
    }

    /* insert if no record exists and return the ID of inserted row */
    r = db_work.exec_prepared("insert_json", hash, json_str);
    db_work.commit();
    if (r.size() == 1 and r[0].size() == 1) {
      return r[0][0].as<int>();
    }
  } catch (const exception & e) {
    print_exception("retrieve_expt_id", e);
  }

  return -1;
}

int run_maintenance_servers()
{
  cerr << "Running maintenance servers" << endl;
  const auto & maintenance_server = src_path / "media-server/maintenance_server";

  ProcessManager proc_manager;

  int server_id = 0;
  for (const auto & expt : config["experiments"]) {
    unsigned int num_servers = expt["num_servers"].as<unsigned int>();

    for (unsigned int i = 0; i < num_servers; i++) {
      server_id++;

      /* run maintenance_server */
      vector<string> args { maintenance_server, yaml_config,
                            to_string(server_id) };
      proc_manager.run_as_child(maintenance_server, args);
    }
  }

  return proc_manager.wait();
}

int run_ws_media_servers()
{
  ProcessManager proc_manager;

  const bool enable_logging = config["enable_logging"].as<bool>();

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

    int expt_id = retrieve_expt_id(json_str);
    unsigned int num_servers = expt["num_servers"].as<unsigned int>();

    cerr << "Running experiment " << expt_id << " on "
         << num_servers << " servers" << endl;

    for (unsigned int i = 0; i < num_servers; i++) {
      server_id++;

      /* run ws_media_server */
      vector<string> args { ws_media_server, yaml_config,
                            to_string(server_id), to_string(expt_id) };
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

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2 and argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* save as global variables */
  yaml_config = fs::absolute(argv[1]);
  config = YAML::LoadFile(yaml_config);
  src_path = fs::canonical(fs::path(
      roost::readlink("/proc/self/exe")).parent_path().parent_path());

  /* simply run maintenance_server(s) */
  if (argc == 3) {
    if (string(argv[2]) != "--maintenance") {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

    return run_maintenance_servers();
  }

  /* run ws_media_server(s) */
  return run_ws_media_servers();
}
