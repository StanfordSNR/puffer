#include <cstdlib>
#include <fcntl.h>

#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include "inotify.hh"
#include "poller.hh"
#include "file_descriptor.hh"
#include "tokenize.hh"
#include "exception.hh"
#include "child_process.hh"

using namespace std;

static vector<string> tmpl;
static const string data_binary_placeholder = "{data_binary}";
static ProcessManager proc_manager;

void print_usage(const string & program_name)
{
  cerr << program_name << " <log path> <log config>" << endl;
}

void post_to_db(const vector<string> & logdata, const string & buf)
{
  vector<string> line = split(buf, " ");

  string data_binary;
  for (const auto & e : logdata) {
    if (e.front() == '{' and e.back() == '}') {
      /* fill in with value from corresponding column */
      int column_no = stoi(e.substr(1));
      data_binary += line.at(column_no - 1);
    } else {
      data_binary += e;
    }
  }

  tmpl.back() = move(data_binary);
  proc_manager.run("curl", tmpl);
}

int tail_loop(const string & log_path, const vector<string> & logdata)
{
  bool new_file = false;
  string buf;

  for (;;) {
    Poller poller;
    Inotify inotify(poller);

    FileDescriptor fd(CheckSystemCall("open (" + log_path + ")",
                                      open(log_path.c_str(), O_RDONLY)));

    inotify.add_watch(log_path, IN_MODIFY | IN_CLOSE_WRITE,
      [&new_file, &fd, &buf, &logdata]
      (const inotify_event & event, const string &) {
        if (event.mask & IN_MODIFY) {
          for (;;) {
            string new_content = fd.read();
            if (new_content.empty()) {
              /* break if nothing more to read */
              break;
            }

            /* find new lines iteratively */
            for (;;) {
              auto pos = new_content.find("\n");
              if (pos == string::npos) {
                buf += new_content;
                new_content = "";
                break;
              } else {
                buf += new_content.substr(0, pos);
                /* buf is a complete line now */
                post_to_db(logdata, buf);

                buf = "";
                new_content = new_content.substr(pos + 1);
              }
            }
          }
        } else if (event.mask & IN_CLOSE_WRITE) {
          /* old log has been closed; open recreated log in next loop */
          new_file = true;
        }
      }
    );

    while (not new_file) {
      auto ret = poller.poll(-1);
      if (ret.result != Poller::Result::Type::Success) {
        return ret.exit_status;
      }
    }

    new_file = false;
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

  string log_path = argv[1];
  /* read a line from the config file */
  ifstream config_file(argv[2]);
  string config_line;
  getline(config_file, config_line);

  /* construct a vector filled with string templates */
  tmpl.emplace_back("curl");
  tmpl.emplace_back("-i");
  tmpl.emplace_back("-XPOST");
  if (const char * influx_key = getenv("INFLUXDB_PASSWORD")) {
    tmpl.emplace_back(
      "http://localhost:8086/write?db=collectd&u=admin&p="
      + string(influx_key) + "&precision=s");
    tmpl.emplace_back("--data-binary");
  } else {
    cerr << "No INFLUXDB_PASSWORD in environment variables" << endl;
    return EXIT_FAILURE;
  }
  tmpl.emplace_back(data_binary_placeholder);

  vector<string> logdata;

  size_t pos = 0;
  while (pos < config_line.size()) {
    size_t left_pos = config_line.find("{", pos);
    if (left_pos == string::npos) {
      logdata.emplace_back(config_line.substr(pos, config_line.size() - pos));
      break;
    }

    if (left_pos - pos > 0) {
      logdata.emplace_back(config_line.substr(pos, left_pos - pos));
    }
    pos = left_pos + 1;

    size_t right_pos = config_line.find("}", pos);
    if (right_pos == string::npos) {
      cerr << "Wrong config format: no matching } for {" << endl;
      return EXIT_FAILURE;
    } else if (right_pos - left_pos == 1) {
      cerr << "Error: empty column number between { and }" << endl;
      return EXIT_FAILURE;
    }

    const auto & column_no = config_line.substr(left_pos + 1,
                                                right_pos - left_pos - 1);
    if (stoi(column_no) <= 0) {
      cerr << "Error: invalid column number between { and }" << endl;
      return EXIT_FAILURE;
    }

    logdata.emplace_back("{" + column_no + "}");
    pos = right_pos + 1;
  }

  /* read new lines from log and post to InfluxDB */
  return tail_loop(log_path, logdata);
}
