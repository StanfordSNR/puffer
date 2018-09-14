#include <cstdlib>
#include <fcntl.h>

#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include "util.hh"
#include "inotify.hh"
#include "poller.hh"
#include "file_descriptor.hh"
#include "filesystem.hh"
#include "tokenize.hh"
#include "exception.hh"
#include "socket.hh"
#include "http_request.hh"
#include "formatter.hh"

using namespace std;
using namespace PollerShortNames;

/* payload data format to post to DB (a "format string" in a vector) */
static Formatter formatter;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <log format> <log path>" << endl;
}

void post_to_db(TCPSocket & db_sock, const vector<string> & lines)
{
  string payload;

  for (const auto & line : lines) {
    vector<string> values = split(line, " ");
    payload += formatter.format(values) + "\n";
  }

  /* send POST request to InfluxDB */
  HTTPRequest request;
  request.set_first_line("POST /write?db=collectd&u=puffer&p="
      + safe_getenv("INFLUXDB_PASSWORD") + "&precision=s HTTP/1.1");
  request.add_header(HTTPHeader{"Host", "localhost:8086"});
  request.add_header(HTTPHeader{"Content-Type", "application/x-www-form-urlencoded"});
  request.add_header(HTTPHeader{"Content-Length", to_string(payload.size())});
  request.done_with_headers();
  request.read_in_body(payload);

  db_sock.write(move(request.str()));
}

int tail_loop(const string & log_path, TCPSocket & db_sock)
{
  vector<string> lines;  /* lines waiting to be posted to InfluxDB */

  Poller poller;

  poller.add_action(Poller::Action(db_sock, Direction::In,
    [&db_sock]()->Result {
      /* must read HTTP responses from InfluxDB, then basically ignore them */
      const string response = db_sock.read();
      if (response.empty()) {
        throw runtime_error("peer socket in InfluxDB has closed");
      }

      return ResultType::Continue;
    }
  ));

  poller.add_action(Poller::Action(db_sock, Direction::Out,
    [&db_sock, &lines]()->Result {
      /* post lines to InfluxDB */
      post_to_db(db_sock, lines);
      lines.clear();

      return ResultType::Continue;
    },
    [&lines]()->bool {
      return not lines.empty();
    }
  ));

  bool log_rotated = false;  /* whether log rotation happened */
  string buf;  /* used to assemble content read from the log into lines */

  Inotify inotify(poller);

  for (;;) {
    FileDescriptor fd(CheckSystemCall("open (" + log_path + ")",
                                      open(log_path.c_str(), O_RDONLY)));
    fd.seek(0, SEEK_END);

    int wd = inotify.add_watch(log_path, IN_MODIFY | IN_CLOSE_WRITE,
        [&log_rotated, &buf, &fd, &db_sock, &lines]
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
            while ((pos = buf.find("\n")) != string::npos) {
              lines.emplace_back(buf.substr(0, pos));
              buf = buf.substr(pos + 1);
            }
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

  if (argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string log_format(argv[1]);
  string log_path(argv[2]);

  /* create an empty log if it does not exist */
  FileDescriptor touch(CheckSystemCall("open (" + log_path + ")",
                       open(log_path.c_str(), O_WRONLY | O_CREAT, 0644)));
  touch.close();

  /* read a line specifying log format and pass into string formatter */
  ifstream format_ifstream(log_format);
  string format_string;
  getline(format_ifstream, format_string);
  assert(format_string.back() != '\n');
  formatter.parse(format_string);

  /* create socket connected to influxdb */
  TCPSocket db_sock;
  Address influxdb_addr("127.0.0.1", 8086);
  db_sock.connect(influxdb_addr);

  /* read new lines from logs and post to InfluxDB */
  return tail_loop(log_path, db_sock);
}
