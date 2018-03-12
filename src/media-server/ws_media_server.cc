#include <fcntl.h>
#include <cstdint>

#include <iostream>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>

#include "yaml-cpp/yaml.h"
#include "filesystem.hh"
#include "file_descriptor.hh"
#include "inotify.hh"
#include "mmap.hh"
#include "timerfd.hh"
#include "ws_server.hh"
#include "ws_client.hh"

using namespace std;
using namespace PollerShortNames;

static fs::path output_path;

static vector<tuple<string, string>> vformats;
static vector<string> aformats;

static map<tuple<string, string>, string> vinit;
static map<string, string> ainit;

static map<uint64_t, map<tuple<string, string>, string>> vdata;
static map<uint64_t, map<string, string>> adata;

static map<uint64_t, WebSocketClient> clients;

static Timerfd global_timer;

static const int clean_time_window = 5400000;

void print_usage(const string & program_name)
{
  cerr << program_name << " <YAML configuration>" << endl;
}

/* get video formats (resolution, CRF) from YAML configuration */
void get_video_formats(const YAML::Node & config,
                       vector<tuple<string, string>> & vformats)
{
  const YAML::Node & res_map = config["video"];
  for (const auto & res_node : res_map) {
    const string & res = res_node.first.as<string>();

    const YAML::Node & crf_list = res_node.second;
    for (const auto & crf_node : crf_list) {
      const string & crf = crf_node.as<string>();
      vformats.emplace_back(res, crf);
    }
  }
}

/* get audio formats (bitrate) from YAML configuration */
void get_audio_formats(const YAML::Node & config, vector<string> & aformats)
{
  const YAML::Node & bitrate_list = config["audio"];
  for (const auto & bitrate_node : bitrate_list) {
    const string & bitrate = bitrate_node.as<string>();
    aformats.emplace_back(bitrate);
  }
}

string mmap_file(const string & filepath)
{
  FileDescriptor fd(CheckSystemCall("open (" + filepath + ")",
                    open(filepath.c_str(), O_RDONLY)));
  size_t size = fd.filesize();
  auto data = mmap_shared(nullptr, size, PROT_READ,
                          MAP_PRIVATE, fd.fd_num(), 0);
  return {static_pointer_cast<char>(data).get(), size};
}

void munmap_files(const string & variable_name, const uint64_t ts)
{
  uint64_t obsolete = 0;
  if (ts > clean_time_window) {
    obsolete = ts - clean_time_window;
  }

  if (variable_name == "vdata") {
    for (auto it = vdata.cbegin(); it != vdata.cend();) {
      if (it->first < obsolete) {
        it = vdata.erase(it);
      } else {
        break;
      }
    }
  } else if (variable_name == "adata") {
    for (auto it = adata.cbegin(); it != adata.cend();) {
      if (it->first < obsolete) {
        it = adata.erase(it);
      } else {
        break;
      }
    }
  }
}

void mmap_video_files(Inotify & inotify)
{
  for (const auto & vformat : vformats) {
    const auto & [res, crf] = vformat;

    string video_dir = output_path / "ready" / (res + "-" + crf);

    inotify.add_watch(video_dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        if (not (event.mask & IN_MOVED_TO)) {
          /* only interested in event IN_MOVED_TO */
          return;
        }

        if (event.mask & IN_ISDIR) {
          /* ignore directories moved into source directory */
          return;
        }

        assert(event.len != 0);

        string filepath = fs::path(path) / event.name;
        string data = mmap_file(filepath);

        string filestem = fs::path(event.name).stem();
        uint64_t ts = stoll(filestem);

        munmap_files("vdata", ts);

        if (filestem == "init") {
          vinit.emplace(vformat, move(data));
        } else {
          vdata[ts][vformat] = move(data);
        }
      }
    );
  }
}

void mmap_audio_files(Inotify & inotify)
{
  for (const auto & aformat : aformats) {
    string audio_dir = output_path / "ready" / aformat;

    inotify.add_watch(audio_dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        if (not (event.mask & IN_MOVED_TO)) {
          /* only interested in event IN_MOVED_TO */
          return;
        }

        if (event.mask & IN_ISDIR) {
          /* ignore directories moved into source directory */
          return;
        }

        assert(event.len != 0);

        string filepath = fs::path(path) / event.name;
        string data = mmap_file(filepath);

        string filestem = fs::path(event.name).stem();
        uint64_t ts = stoll(filestem);

        munmap_files("adata", ts);

        if (filestem == "init") {
          ainit.emplace(aformat, move(data));
        } else {
          adata[ts][aformat] = move(data);
        }
      }
    );
  }
}

void start_global_timer(WebSocketServer & ws_server)
{
  Poller & poller = ws_server.poller();

  poller.add_action(
    Poller::Action(global_timer, Direction::In,
      [&]() {
        if (global_timer.expirations() > 0) {
          /* iterate over all connections */
          for (auto & client_item : clients) {
            const uint64_t connection_id = client_item.first;
            auto & client = client_item.second;

            /* TODO: for debugging only */
            if (client.playback_buf() < 10) {
              uint64_t next_vts = client.next_vts();

              const auto it = vdata.find(next_vts);
              if (it == vdata.end()) {
                continue;
              }

              /* pick the first video format for debugging */
              const auto & data = it->second.begin()->second;
              WSFrame frame {true, WSFrame::OpCode::Binary, data};
              ws_server.queue_frame(connection_id, frame);

              client.set_next_vts(next_vts + 180180);
            }
          }
        }

        return ResultType::Continue;
      }
    )
  );

  /* the timer fires every 10 ms */
  global_timer.start(10, 10);
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

  /* parse YAML configuration */
  YAML::Node config = YAML::LoadFile(argv[1]);
  get_video_formats(config, vformats);
  get_audio_formats(config, aformats);

  string output_dir = config["output"].as<string>();
  output_path = fs::path(output_dir);

  string ip = "0.0.0.0";
  uint16_t port = 8080;

  WebSocketServer ws_server {{ip, port}};

  /* mmap new media files */
  Inotify inotify(ws_server.poller());
  mmap_video_files(inotify);
  mmap_audio_files(inotify);

  /* start the global timer */
  start_global_timer(ws_server);

  ws_server.set_message_callback(
    [&ws_server](const uint64_t connection_id, const WSMessage & message)
    {
      cerr << "Message (from=" << connection_id << "): "
           << message.payload() << endl;
    }
  );

  ws_server.set_open_callback(
    [](const uint64_t connection_id)
    {
      cerr << "Connected (id=" << connection_id << ")" << endl;

      auto ret = clients.emplace(connection_id,
                                 WebSocketClient(connection_id, 0, 0));
      if (not ret.second) {
        throw runtime_error("Connection ID " + to_string(connection_id) +
                            " already exists");
      }
    }
  );

  ws_server.set_close_callback(
    [](const uint64_t connection_id)
    {
      cerr << "Connection closed (id=" << connection_id << ")" << endl;

      clients.erase(connection_id);
    }
  );

  while (ws_server.loop_once().result == Poller::Result::Type::Success);

  return EXIT_SUCCESS;
}
