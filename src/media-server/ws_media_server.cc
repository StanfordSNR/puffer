#include <fcntl.h>
#include <cstdint>

#include <iostream>
#include <string>
#include <map>
#include <memory>

#include "filesystem.hh"
#include "yaml-cpp/yaml.h"
#include "inotify.hh"
#include "ws_server.hh"
#include "mmap.hh"
#include "file_descriptor.hh"

using namespace std;

static fs::path output_path;

static vector<tuple<string, string>> vformats;
static vector<string> aformats;

static map<int64_t, map<tuple<string, string>, string>> vdata;
static map<int64_t, map<string, string>> adata;

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
        cout << data << endl;

        string filestem = fs::path(event.name).stem();
        if (filestem == "init") {
          vdata[-1][vformat] = move(data);
        } else {
          vdata[stoll(filestem)][vformat] = move(data);
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
        if (filestem == "init") {
          adata[-1][aformat] = move(data);
        } else {
          adata[stoll(filestem)][aformat] = move(data);
        }
      }
    );
  }
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
  Poller & poller = ws_server.poller();
  Inotify inotify(poller);
  mmap_video_files(inotify);
  mmap_audio_files(inotify);

  ws_server.set_message_callback(
    [&ws_server](const uint64_t connection_id, const WSMessage & message)
    {
      cerr << "Message (from=" << connection_id << "): "
           << message.payload() << endl;

      WSFrame echo_frame {true, message.type(), message.payload()};
      ws_server.queue_frame(connection_id, echo_frame);
    }
  );

  ws_server.set_open_callback(
    [](const uint64_t connection_id)
    {
      cerr << "Connected (id=" << connection_id << ")" << endl;
    }
  );

  ws_server.set_close_callback(
    [](const uint64_t connection_id)
    {
      cerr << "Connection closed (id=" << connection_id << ")" << endl;
    }
  );

  while (ws_server.loop_once().result == Poller::Result::Type::Success);

  return EXIT_SUCCESS;
}
