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

static map<tuple<string, string>, tuple<shared_ptr<void>, size_t>> vinit;
static map<string, tuple<shared_ptr<void>, size_t>> ainit;

static map<uint64_t,
           map<tuple<string, string>, tuple<shared_ptr<void>, size_t>>> vdata;
static map<uint64_t, map<string, tuple<shared_ptr<void>, size_t>>> adata;

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

tuple<shared_ptr<void>, size_t> mmap_file(const string & filepath)
{
  FileDescriptor fd(CheckSystemCall("open (" + filepath + ")",
                    open(filepath.c_str(), O_RDONLY)));
  size_t size = fd.filesize();
  auto data = mmap_shared(nullptr, size, PROT_READ,
                          MAP_PRIVATE, fd.fd_num(), 0);
  return {data, size};
}

void munmap_video(const uint64_t ts)
{
  uint64_t obsolete = 0;
  if (ts > clean_time_window) {
    obsolete = ts - clean_time_window;
  }

 for (auto it = vdata.cbegin(); it != vdata.cend();) {
   if (it->first < obsolete) {
     it = vdata.erase(it);
   } else {
     break;
   }
 }
}

void munmap_audio(const uint64_t ts)
{
  uint64_t obsolete = 0;
  if (ts > clean_time_window) {
    obsolete = ts - clean_time_window;
  }

  for (auto it = adata.cbegin(); it != adata.cend();) {
    if (it->first < obsolete) {
      it = adata.erase(it);
    } else {
      break;
    }
  }
}

static void do_mmap_video(const fs::path & filepath,
                          const tuple<string, string> & vformat)
{
  auto data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    vinit.emplace(vformat, data_size);
  } else {
    uint64_t ts = stoll(filestem);
    munmap_video(ts);
    vdata[ts][vformat] = data_size;
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

        fs::path filepath = fs::path(path) / event.name;
        do_mmap_video(filepath, vformat);
      }
    );

    /* process existing files */
    for (const auto & file : fs::directory_iterator(video_dir)) {
      do_mmap_video(file.path(), vformat);
    }
  }
}

static void do_mmap_audio(const fs::path & filepath, const string & aformat)
{
  auto data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    ainit.emplace(aformat, data_size);
  } else {
    uint64_t ts = stoll(filestem);
    munmap_audio(ts);
    adata[ts][aformat] = data_size;
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

        fs::path filepath = fs::path(path) / event.name;
        do_mmap_audio(filepath, aformat);
      }
    );

    /* process existing files */
    for (const auto & file : fs::directory_iterator(audio_dir)) {
      do_mmap_audio(file.path(), aformat);
    }
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
              const auto & [data, size] = it->second.begin()->second;

              WSFrame frame {true, WSFrame::OpCode::Binary,
                             {static_pointer_cast<char>(data).get(), size}};
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
    [](const uint64_t connection_id, const WSMessage & message)
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
