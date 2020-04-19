#include <cstdlib>
#include <fcntl.h>

#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include <fstream>

#include "util.hh"
#include "yaml-cpp/yaml.h"
#include "inotify.hh"
#include "poller.hh"
#include "file_descriptor.hh"
#include "filesystem.hh"
#include "tokenize.hh"
#include "exception.hh"
#include "formatter.hh"
#include "influxdb_client.hh"
#include "timestamp.hh"

using namespace std;
using namespace PollerShortNames;

/* payload data format to post to DB (a "format string" in a vector) */
static Formatter formatter;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <YAML configuration> <log format> <log path>"
  << endl;
}

tuple<string, string> enforce_unique(const string & line,
                                     deque<uint64_t> & unique_ns)
{
  auto last_space = line.rfind(' ');
  string before_last_space = line.substr(0, last_space);
  string orig_ts_str = line.substr(last_space + 1);

  const uint64_t orig_ts_ns = stoll(orig_ts_str) * MILLION;  /* ms -> ns */

  /* pop if ts in ms < orig_new_ts in ms */
  while (not unique_ns.empty()) {
    const auto ts_ns = unique_ns.front();
    if (ts_ns / MILLION < orig_ts_ns / MILLION) {
      unique_ns.pop_front();
    } else {
      break;
    }
  }

  /* increment the last ts in unique_ns by 1 ns */
  uint64_t new_ts_ns = orig_ts_ns;
  if (not unique_ns.empty()) {
    const auto ts_ns = unique_ns.back();

    if ((ts_ns + 1) / MILLION != new_ts_ns / MILLION) {
      throw runtime_error("enforce_unique: " + to_string(ts_ns) +
                          ", " + to_string(new_ts_ns));
    }

    new_ts_ns = ts_ns + 1;
  }
  unique_ns.push_back(new_ts_ns);

  if (new_ts_ns == orig_ts_ns) {
    /* no duplicate timestamp exists */
    return make_tuple(line, "ms");
  } else {
    /* duplicate timestamp exists; post the incremented timestamp */
    string new_line = before_last_space + " " + to_string(new_ts_ns);
    return make_tuple(new_line, "ns");
  }
}

int tail_loop(const YAML::Node & config, const string & log_path,
              const string & measurement)
{
  /* enforce uniqueness for the crucial measurements below */
  bool crucial_measurements = (measurement == "client_buffer" ||
                               measurement == "video_acked" ||
                               measurement == "video_sent");
  deque<uint64_t> unique_ns;

  Poller poller;
  Inotify inotify(poller);

  const auto & influx = config["influxdb_connection"];
  InfluxDBClient influxdb_client(
      poller,
      {influx["host"].as<string>(), to_string(influx["port"].as<uint16_t>())},
      influx["dbname"].as<string>(),
      influx["user"].as<string>(),
      safe_getenv(influx["password"].as<string>()));

  bool log_rotated = false;  /* whether log rotation happened */
  string buf;  /* used to assemble content read from the log into lines */

  for (;;) {
    FileDescriptor fd(CheckSystemCall("open (" + log_path + ")",
                                      open(log_path.c_str(), O_RDONLY)));
    fd.seek(0, SEEK_END);

    int wd = inotify.add_watch(log_path, IN_MODIFY | IN_CLOSE_WRITE,
      [&log_rotated, &buf, &fd, &influxdb_client,
       crucial_measurements, &unique_ns]
      (const inotify_event & event, const string &) {
        if (event.mask & IN_MODIFY) {
          string new_content = fd.read();
          if (new_content.empty()) {
            /* return if nothing more to read */
            return;
          }
          buf += new_content;

          /* find new lines iteratively */
          size_t pos = 0;
          string payload;

          while ((pos = buf.find("\n")) != string::npos) {
            const string & line = buf.substr(0, pos);
            vector<string> values = split(line, ",");

            /* enforce data uniqueness for crucial measurements */
            if (crucial_measurements) {
              auto [new_line, precision] = enforce_unique(
                  formatter.format(values), unique_ns);
              if (precision == "ms") {
                payload += new_line + "\n";
              } else {
                /* post data points with other time precisions immediately */
                influxdb_client.post(new_line, precision);
              }
            } else {
              payload += formatter.format(values) + "\n";
            }

            buf = buf.substr(pos + 1);
          }

          /* post aggregated lines (with time precision 'ms') */
          influxdb_client.post(payload);
        } else if (event.mask & IN_CLOSE_WRITE) {
          /* old log was closed; open and watch new log in next loop */
          log_rotated = true;
        }
      }
    );

    while (not log_rotated) {
      auto ret = poller.poll(-1);
      if (ret.result != Poller::Result::Type::Success) {
        return ret.exit_status;
      }
    }

    inotify.rm_watch(wd);
    log_rotated = false;
  }

  return EXIT_SUCCESS;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 4) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  YAML::Node config = YAML::LoadFile(argv[1]);
  if (not config["enable_logging"].as<bool>()) {
    cerr << "Error: logging is not enabled yet" << endl;
    return EXIT_FAILURE;
  }

  string log_format(argv[2]);
  string log_path(argv[3]);

  /* create an empty log if it does not exist */
  FileDescriptor touch(CheckSystemCall("open (" + log_path + ")",
                       open(log_path.c_str(), O_WRONLY | O_CREAT, 0644)));
  touch.close();

  /* read a line specifying log format and pass into string formatter */
  ifstream format_ifstream(log_format);
  string format_string;
  getline(format_ifstream, format_string);
  formatter.parse(format_string);

  auto comma_pos = format_string.find(',');
  string measurement = format_string.substr(0, comma_pos);

  /* read new lines from logs and post to InfluxDB */
  return tail_loop(config, log_path, measurement);
}
