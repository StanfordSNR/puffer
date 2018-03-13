#include <fcntl.h>
#include <cstdint>

#include <iostream>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>

#include "yaml-cpp/yaml.h"
#include "client_message.h"
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

static map<tuple<string, string>, tuple<shared_ptr<char>, size_t>> vinit;
static map<string, tuple<shared_ptr<char>, size_t>> ainit;

static map<uint64_t,
           map<tuple<string, string>, tuple<shared_ptr<char>, size_t>>> vdata;
static map<uint64_t, map<string, tuple<shared_ptr<char>, size_t>>> adata;

static map<uint64_t, WebSocketClient> clients;

static Timerfd global_timer;

static const int clean_time_window = 5400000;

static const int VIDEO_TIMESCALE = 90000;

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

tuple<shared_ptr<char>, size_t> mmap_file(const string & filepath)
{
  FileDescriptor fd(CheckSystemCall("open (" + filepath + ")",
                    open(filepath.c_str(), O_RDONLY)));
  size_t size = fd.filesize();
  shared_ptr<void> data = mmap_shared(nullptr, size, PROT_READ,
                                      MAP_PRIVATE, fd.fd_num(), 0);
  return {static_pointer_cast<char>(data), size};
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

static tuple<string, string> select_video_quality(WebSocketClient & client)
{
  return vformats[0]; // TODO: choose the first one for now
}

static string select_audio_quality(WebSocketClient & client) 
{
  return aformats[0];
}

static void send_video_to_client(WebSocketServer & ws_server,
                                 WebSocketClient & client)
{
  uint64_t next_vts = client.next_vts();

  tuple<string, string> next_vq = select_video_quality(client);

  const auto vts_map = vdata.find(next_vts);
  if (vts_map == vdata.end()) {
    return;
  }

  const auto mmapped_video = vts_map.find(next_vq);
  if (mmapped_video == vts_map.end()) {
    return;
  }

  const auto & [video_data, video_size] = mmapped_video;
  const auto & [init_data, init_size] = vinit[next_vq];

  /* Compute size of frame payload excluding header */
  const bool init_required = !client.curr_vq.has_value() || next_vq != client.curr_vq().value();
  size_t payload_len = video_size;
  if (init_required) {
    payload_len += init_size;
  }

  /* Make the metadata at the start of a frame */
  string metadata = make_video_msg(next_vq, next_vts, 180180, 0,
                                   payload_len);

  /* Copy data into payload string */
  string frame_payload(metadata.length() + payload_len);
  memcpy(&frame_payload[0], &metadata[0], metadata.length());
  int video_segment_begin;
  if (init_required) {
    memcpy(&frame_payload[metadata.length()], init_data.get(), init_size);
    video_segment_begin = metadata.length() + init_size;
  } else {
    video_segment_begin = metadata.length();
  }
  memcpy(&frame_payload[video_segment_begin], video_data.get(), video_size);

  // TODO: fragment this further
  WSFrame frame {true, WSFrame::OpCode::Binary, frame_payload};
  ws_server.queue_frame(client.connection_id(), frame);

  // TODO: stop hardcoding these numbers
  client.set_next_vts(next_vts + 180180);
  client.set_next_vq(next_vq);
}

static void send_audio_to_client(WebSocketServer & ws_server,
                                 WebSocketClient & client) 
{
  uint64_t next_ats = client.next_ats();

  tuple<string, string> next_aq = select_audio_quality(client);

  const auto ats_map = adata.find(next_ats);
  if (ats_map == adata.end()) {
    return;
  }

  const auto mmapped_audio = ats_map.find(next_aq);
  if (mmapped_audio == ats_map.end()) {
    return;
  }

  const auto & [audio_data, audio_size] = mmapped_audio;
  const auto & [init_data, init_size] = ainit[next_aq];

  /* Compute size of frame payload excluding header */
  const bool init_required = !client.curr_aq.has_value() || next_aq != client.curr_aq().value();
  size_t payload_len = audio_size;
  if (init_required) {
    payload_len += init_size;
  }

  /* Make the metadata at the start of a frame */
  string metadata = make_audio_msg(next_aq, next_ats, 48000, 0, 
                                   payload_len);

  /* Copy data into payload string */
  string frame_payload(metadata.length() + payload_len);
  memcpy(&frame_payload[0], &metadata[0], metadata.length());
  int audio_segment_begin;
  if (init_required) {
    memcpy(&frame_payload[metadata.length()], init_data.get(), init_size);
    audio_segment_begin = metadata.length() + init_size;
  } else {
    audio_segment_begin = metadata.length();
  }
  memcpy(&frame_payload[audio_segment_begin], audio_data.get(), audio_size);

  WSFrame frame {true, WSFrame::OpCode::Binary, frame_payload};
  ws_server.queue_frame(client.connection_id(), frame);

  client.set_next_ats(next_vts + 48000);
  client.set_curr_aq(next_aq);
}

static void serve_client(WebSocketServer & ws_server, WebSocketClient & client) 
{
  serve_video_to_client(ws_server, client);
  serve_audio_to_client(ws_server, client);
}

void start_global_timer(WebSocketServer & ws_server)
{
  /* the timer fires every 100 ms */
  global_timer.start(100, 100);

  ws_server.poller().add_action(
    Poller::Action(global_timer, Direction::In,
      [&]() {
        if (global_timer.expirations() > 0) {
          /* iterate over all connections */
          for (auto & client_item : clients) {
            const uint64_t connection_id = client_item.first;
            auto & client = client_item.second;
            serve_client(ws_server, client);
          }
        }

        return ResultType::Continue;
      }
    )
  );
}

void handle_client_init(WebSocketServer & ws_server, WebSocketClient & client,
                        const ClientInit & message)
{
  const string channel = message.channel;
  // TODO: if channel is invalid, kill the connection

  // TODO: compute init ats, vts
  client.initialize(channel, init_vts, init_ats);

  string reply = make_server_init_msg(
    channel,
    "video/mp4; codecs=\"avc1.42E020\"", // TODO: read these from somewhere
    "audio/webm; codecs=\"opus\"",
    VIDEO_TIMESCALE,
    client.init_vts);

  /* Reinitialize video playback on the client */
  WSFrame frame {true, WSFrame::OpCode::Binary, reply};
  ws_server.queue_frame(client.connection_id(), frame);
}

void handle_client_info(WebSocketServer & ws_server, WebSocketClient & client,
                        const ClientInfo & message)
{
  client.set_audio_playback_buf(message.audio_buffer_len);
  client.set_video_playback_buf(message.video_buffer_len);
}

void handle_client_open(WebSocketServer & ws_server, uint64_t & connection_id)
{
  /* Send the client the list of playable channels */
  string server_hello = make_server_hello_msg({""}); // TODO: get channels from somewhere
  WSFrame frame {true, WSFrame::OpCode::Binary, server_hello};
  ws_server.queue_frame(connection_id, frame);
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
  uint16_t port = 8080; // TODO read this from somewhere

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

      auto client = clients[connection_id];
      try {
        const auto data = unpack_client_msg(message.payload());
        switch (data.first) {
          case ClientMsg::Init: {
            ClientInit client_init = parse_client_init_msg(data.second);
            handle_client_init_msg(ws_server, client, client_init);
            break;
          }
          case ClientMsg::Info: {
            ClientInfo client_info = parse_client_info_msg(data.second);
            handle_client_info_msg(ws_server, client, client_info);
            break;
          }
          default ClientMsg::Unknown:
            break;
        }
      } catch (const ParseExeception & e) {
        // TODO: close the client
      }
    }
  );

  ws_server.set_open_callback(
    [](const uint64_t connection_id)
    {
      cerr << "Connected (id=" << connection_id << ")" << endl;
      handle_client_open(ws_server, connection_id);
      auto ret = clients.emplace(connection_id, WebSocketClient(connection_id));
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

  for (;;) {
    /* TODO: why does this return Poller::Result::Type::Timeout or ::Exit? */
    ws_server.loop_once();
  }

  return EXIT_SUCCESS;
}
