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
#include "tokenize.hh"
#include "exception.hh"
#include "system_runner.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << program_name << " <log path> <log config>" << endl;
}

void post_to_db(vector<string> & cmd, const vector<string> & data,
                const string & buf)
{
  vector<string> line = split(buf, " ");

  string data_str;
  for (const auto & e : data) {
    if (e.front() == '{' and e.back() == '}') {
      /* fill in with value from corresponding column */
      unsigned int column_idx = stoi(e.substr(1)) - 1;
      if (column_idx >= line.size()) {
        cerr << "Silent error: invalid column " << column_idx + 1 << endl;
        return;
      }

      data_str += line.at(column_idx);
    } else {
      data_str += e;
    }
  }

  cmd.back() = move(data_str);
  run("curl", cmd);
}

int tail_loop(const string & log_path, vector<string> & cmd,
              const vector<string> & data)
{
  bool new_file = false;
  string buf;

  for (;;) {
    Poller poller;
    Inotify inotify(poller);

    /* read new lines from the end */
    FileDescriptor fd(CheckSystemCall("open (" + log_path + ")",
                                      open(log_path.c_str(), O_RDONLY)));
    fd.seek(0, SEEK_END);

    inotify.add_watch(log_path, IN_MODIFY | IN_CLOSE_WRITE,
      [&new_file, &buf, &fd, &cmd, &data]
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
                post_to_db(cmd, data, buf);

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

  /* create an empty log if it does not exist */
  string log_path = argv[1];
  FileDescriptor touch(CheckSystemCall("open (" + log_path + ")",
                       open(log_path.c_str(), O_WRONLY | O_CREAT, 0644)));
  touch.close();

  /* read a line from the config file */
  ifstream config_file(argv[2]);
  string config_line;
  getline(config_file, config_line);

  /* construct a command vector, with a placeholder to update at the end */
  vector<string> cmd { "curl", "-i", "-XPOST",
    "http://localhost:8086/write?db=collectd&u=admin&p="
    + safe_getenv("INFLUXDB_PASSWORD") + "&precision=s",
    "--data-binary", "{data_binary}"};

  /* data for "--data-binary": store a "format string" in a vector */
  vector<string> data;

  size_t pos = 0;
  while (pos < config_line.size()) {
    size_t left_pos = config_line.find("{", pos);
    if (left_pos == string::npos) {
      data.emplace_back(config_line.substr(pos, config_line.size() - pos));
      break;
    }

    if (left_pos - pos > 0) {
      data.emplace_back(config_line.substr(pos, left_pos - pos));
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

    data.emplace_back("{" + column_no + "}");
    pos = right_pos + 1;
  }

  /* read new lines from log and post to InfluxDB */
  return tail_loop(log_path, cmd, data);
}
