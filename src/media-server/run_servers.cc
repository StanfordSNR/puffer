#include <iostream>
#include <string>
#include <memory>
#include <crypto++/sha.h>
#include <crypto++/hex.h>
#include <pqxx/pqxx>

#include "yaml.hh"
#include "filesystem.hh"
#include "path.hh"
#include "child_process.hh"
#include "system_runner.hh"
#include "influxdb_client.hh"
#include "util.hh"
#include "exception.hh"

using namespace std;
using namespace CryptoPP;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <YAML configuration> <number of servers>"
  << endl;
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

int retrieve_expt_id(const YAML::Node & config, const string & json_str)
{
  /* compute SHA-256 */
  string hash = sha256(json_str);
  cerr << "Experiment checksum: " << hash << endl;

  try {
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
    pqxx::result r = db_work.prepared("select_id")(hash).exec();
    if (r.size() == 1 and r[0].size() == 1) {
      /* the same hash already exists */
      return r[0][0].as<int>();
    }

    /* insert if no record exists and return the ID of inserted row */
    r = db_work.prepared("insert_json")(hash)(json_str).exec();
    db_work.commit();
    if (r.size() == 1 and r[0].size() == 1) {
      return r[0][0].as<int>();
    }
  } catch (const exception & e) {
    print_exception("retrieve_expt_id", e);
  }

  return -1;
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

  const auto & src_path = fs::canonical(fs::path(
      roost::readlink("/proc/self/exe")).parent_path().parent_path());

  const auto & expt_json = src_path / "media-server/expt_json.py";
  string json_str = run(expt_json, {expt_json, yaml_config}, true).first;

  /* upload JSON to and retrieve an experimental ID from PostgreSQL */
  int expt_id = retrieve_expt_id(config, json_str);
  cerr << "Experiment ID: " << expt_id << endl;

  int num_servers = stoi(argv[2]);
  if (num_servers <= 0) {
    cerr << "<number of servers>: a positive integer is required" << endl;
    return EXIT_FAILURE;
  }

  const bool enable_logging = config["enable_logging"].as<bool>();

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
