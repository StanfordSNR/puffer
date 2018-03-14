#include <fcntl.h>
#include <cstdint>

#include <iostream>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>

#include "yaml.hh"
#include "filesystem.hh"
#include "file_descriptor.hh"
#include "inotify.hh"
#include "mmap.hh"
#include "timerfd.hh"
#include "message.hh"
#include "ws_server.hh"
#include "ws_client.hh"

using namespace std;
using namespace PollerShortNames;

using mmap_t = tuple<shared_ptr<char>, size_t>;

static fs::path output_path;

static vector<VideoFormat> vformats;
static vector<AudioFormat> aformats;

static map<VideoFormat, mmap_t> vinit;
static map<AudioFormat, mmap_t> ainit;
static map<uint64_t, map<VideoFormat, mmap_t>> vdata;
static map<uint64_t, map<AudioFormat, mmap_t>> adata;

static map<uint64_t, WebSocketClient> clients;

static Timerfd global_timer;

static unsigned int CLEAN_TIME_WINDOW;
static unsigned int TIMESCALE;
static unsigned int VIDEO_DURATION;
static unsigned int AUDIO_DURATION;
static string VIDEO_CODEC;
static string AUDIO_CODEC;
static optional<uint64_t> INIT_VTS;

void print_usage(const string & program_name)
{
  cerr << program_name << " <YAML configuration>" << endl;
}

mmap_t mmap_file(const string & filepath)
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
  if (ts > CLEAN_TIME_WINDOW) {
    obsolete = ts - CLEAN_TIME_WINDOW;
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
  if (ts > CLEAN_TIME_WINDOW) {
    obsolete = ts - CLEAN_TIME_WINDOW;
  }

  for (auto it = adata.cbegin(); it != adata.cend();) {
    if (it->first < obsolete) {
      it = adata.erase(it);
    } else {
      break;
    }
  }
}

void do_mmap_video(const fs::path & filepath, const VideoFormat & vf)
{
  auto data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    vinit.emplace(vf, data_size);
  } else {
    uint64_t ts = stoll(filestem);
    munmap_video(ts);
    vdata[ts][vf] = data_size;
  }
}

void mmap_video_files(Inotify & inotify)
{
  for (const auto & vf : vformats) {
    string video_dir = output_path / "ready" / vf.to_string();

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
        do_mmap_video(filepath, vf);
      }
    );

    /* process existing files */
    for (const auto & file : fs::directory_iterator(video_dir)) {
      do_mmap_video(file.path(), vf);
    }
  }
}

void do_mmap_audio(const fs::path & filepath, const AudioFormat & af)
{
  auto data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    ainit.emplace(af, data_size);
  } else {
    uint64_t ts = stoll(filestem);
    munmap_audio(ts);
    adata[ts][af] = data_size;
  }
}

void mmap_audio_files(Inotify & inotify)
{
  for (const auto & af : aformats) {
    string audio_dir = output_path / "ready" / af.to_string();

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
        do_mmap_audio(filepath, af);
      }
    );

    /* process existing files */
    for (const auto & file : fs::directory_iterator(audio_dir)) {
      do_mmap_audio(file.path(), af);
    }
  }
}

const VideoFormat select_video_quality(WebSocketClient & client)
{
  // TODO: make a real choice
  return client.curr_vq().value_or(vformats[0]);
}

const AudioFormat select_audio_quality(WebSocketClient & client)
{
  // TODO: make a real choice
  return client.curr_aq().value_or(aformats[0]);
}

void serve_video_to_client(WebSocketServer & server, WebSocketClient & client)
{
  uint64_t next_vts = client.next_vts().value();
  const auto vdata_it = vdata.find(next_vts);
  if (vdata_it == vdata.end()) {
    return;
  }

  const auto & vts_map = vdata_it->second;

  const VideoFormat next_vq = select_video_quality(client);
  const auto vts_map_it = vts_map.find(next_vq);
  if (vts_map_it == vts_map.end()) {
    return;
  }

  const auto & [video_data, video_size] = vts_map_it->second;
  const auto & [init_data, init_size] = vinit.at(next_vq);

  /* Compute size of frame payload excluding header */
  const bool init_segment_required = (not client.curr_vq().has_value() or
                                      next_vq != client.curr_vq().value());
  size_t payload_len = video_size;
  if (init_segment_required) {
    payload_len += init_size;
  }

  /* Make the metadata at the start of a frame */
  string metadata = make_video_msg(next_vq.to_string(), next_vts, VIDEO_DURATION,
                                   0, /* payload start offset */
                                   payload_len);

  /* Copy data into payload string */
  string frame_payload(metadata.length() + payload_len, 0);
  memcpy(&frame_payload[0], &metadata[0], metadata.length());
  int video_segment_begin;
  if (init_segment_required) {
    memcpy(&frame_payload[metadata.length()], init_data.get(), init_size);
    video_segment_begin = metadata.length() + init_size;
  } else {
    video_segment_begin = metadata.length();
  }
  memcpy(&frame_payload[video_segment_begin], video_data.get(), video_size);

  // TODO: fragment this further
  WSFrame frame {true, WSFrame::OpCode::Binary, frame_payload};
  server.queue_frame(client.connection_id(), frame);

  client.set_next_vts(next_vts + VIDEO_DURATION);
  client.set_curr_vq(next_vq);
}

void serve_audio_to_client(WebSocketServer & server, WebSocketClient & client)
{
  uint64_t next_ats = client.next_ats().value();
  const auto adata_it = adata.find(next_ats);
  if (adata_it == adata.end()) {
    return;
  }

  const auto & ats_map = adata_it->second;

  const AudioFormat next_aq = select_audio_quality(client);
  const auto ats_map_it = ats_map.find(next_aq);
  if (ats_map_it == ats_map.end()) {
    return;
  }

  const auto & [audio_data, audio_size] = ats_map_it->second;
  const auto & [init_data, init_size] = ainit[next_aq];

  /* Compute size of frame payload excluding header */
  const bool init_segment_required = (not client.curr_aq().has_value() or
                                      next_aq != client.curr_aq().value());
  size_t payload_len = audio_size;
  if (init_segment_required) {
    payload_len += init_size;
  }

  /* Make the metadata at the start of a frame */
  string metadata = make_audio_msg(next_aq.to_string(), next_ats, AUDIO_DURATION,
                                   0, /* payload start offset */
                                   payload_len);

  /* Copy data into payload string */
  string frame_payload(metadata.length() + payload_len, 0);
  memcpy(&frame_payload[0], &metadata[0], metadata.length());
  int audio_segment_begin;
  if (init_segment_required) {
    memcpy(&frame_payload[metadata.length()], init_data.get(), init_size);
    audio_segment_begin = metadata.length() + init_size;
  } else {
    audio_segment_begin = metadata.length();
  }
  memcpy(&frame_payload[audio_segment_begin], audio_data.get(), audio_size);

  WSFrame frame {true, WSFrame::OpCode::Binary, frame_payload};
  server.queue_frame(client.connection_id(), frame);

  client.set_next_ats(next_ats + AUDIO_DURATION);
  client.set_curr_aq(next_aq);
}

void serve_client(WebSocketServer & server, WebSocketClient & client)
{
  assert (client.channel().has_value());
  assert (client.next_vts().has_value());
  assert (client.next_ats().has_value());
  serve_video_to_client(server, client);
  serve_audio_to_client(server, client);
}

void start_global_timer(WebSocketServer & server)
{
  /* the timer fires every 100 ms */
  global_timer.start(100, 100);

  server.poller().add_action(
    Poller::Action(global_timer, Direction::In,
      [&]() {
        if (global_timer.expirations() > 0) {
          /* iterate over all connections */
          for (auto & client_item : clients) {
            WebSocketClient & client = client_item.second;
            if (client.channel().has_value()) {
              serve_client(server, client);
            }
          }
        }

        return ResultType::Continue;
      }
    )
  );
}

uint64_t get_init_vts() {
  if (INIT_VTS.has_value()) {
    return INIT_VTS.value(); /* The user configured a fixed VTS */
  } else {
    /* Choose the newest vts with all qualities available */
    for (auto it = vdata.rbegin(); it != vdata.rend(); ++it) {
      if (it->second.size() == vformats.size()) {
        return it->first;
      }
    }
    cerr << "Encoder is in a bad state, no vts has all qualities available" << endl;
    abort();
  }
}

inline uint64_t get_init_ats(const uint64_t vts) {
  return (vts / AUDIO_DURATION) * AUDIO_DURATION;
}

void handle_client_init(WebSocketServer & server, WebSocketClient & client,
                        const ClientInitMessage & message)
{
  const string channel = message.channel;
  // TODO: if channel is invalid, kill the connection
  if (false) {
    throw BadClientMessageException("Invalid channel");
  }

  uint16_t init_vts = get_init_vts();
  uint16_t init_ats = get_init_ats(init_vts);

  client.init(channel, init_vts, init_ats);

  string reply = make_server_init_msg(channel, VIDEO_CODEC, AUDIO_CODEC,
                                      TIMESCALE, client.next_vts().value());

  /* Reinitialize video playback on the client */
  WSFrame frame {true, WSFrame::OpCode::Binary, reply};
  server.queue_frame(client.connection_id(), frame);
}

void handle_client_info(WebSocketClient & client, const ClientInfoMessage & message)
{
  client.set_audio_playback_buf(message.audio_buffer_len);
  client.set_video_playback_buf(message.video_buffer_len);
}

void handle_client_open(WebSocketServer & server, const uint64_t connection_id)
{
  /* Send the client the list of playable channels */
  string server_hello = make_server_hello_msg({""}); // TODO: get channels from somewhere
  WSFrame frame {true, WSFrame::OpCode::Binary, server_hello};
  server.queue_frame(connection_id, frame);
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

  /* load YAML configuration */
  YAML::Node config = load_yaml(argv[1]);
  vector<VideoFormat> vformats = get_video_formats(config);
  vector<AudioFormat> aformats = get_audio_formats(config);

  string output_dir = config["output"].as<string>();
  output_path = fs::path(output_dir);

  CLEAN_TIME_WINDOW = config["clean_time_window"].as<int>();
  TIMESCALE = config["timescale"].as<int>();
  VIDEO_DURATION = config["video_duration"].as<int>();
  AUDIO_DURATION = config["audio_duration"].as<int>();
  VIDEO_CODEC = config["video_codec"].as<string>();
  AUDIO_CODEC = config["audio_codec"].as<string>();

  if (config["init_vts"]) {
    INIT_VTS = config["init_vts"].as<int>();
  }

  /* create a WebSocketServer instance */
  string ip = "0.0.0.0";
  uint16_t port = config["port"].as<int>();
  WebSocketServer server {{ip, port}};

  /* mmap new media files */
  Inotify inotify(server.poller());
  mmap_video_files(inotify);
  mmap_audio_files(inotify);

  /* start the global timer */
  start_global_timer(server);

  server.set_message_callback(
    [&](const uint64_t connection_id, const WSMessage & message)
    {
      cerr << "Message (from=" << connection_id << "): "
           << message.payload() << endl;

      auto client = clients.at(connection_id);

      try {
        const auto data = unpack_client_msg(message.payload());
        switch (data.first) {
          case ClientMessage::Init: {
            ClientInitMessage client_init = parse_client_init_msg(data.second);
            handle_client_init(server, client, client_init);
            break;
          }
          case ClientMessage::Info: {
            ClientInfoMessage client_info = parse_client_info_msg(data.second);
            handle_client_info(client, client_info);
            break;
          }
          default:
            break;
        }
      } catch (const BadClientMessageException & e) {
        cerr << "Bad message from client: " << e.what() << endl;
        clients.erase(connection_id);
      }
    }
  );

  server.set_open_callback(
    [&](const uint64_t connection_id)
    {
      cerr << "Connected (id=" << connection_id << ")" << endl;

      handle_client_open(server, connection_id);
      auto ret = clients.emplace(connection_id, WebSocketClient(connection_id));
      if (not ret.second) {
        throw runtime_error("Connection ID " + to_string(connection_id) +
                            " already exists");
      }
    }
  );

  server.set_close_callback(
    [&](const uint64_t connection_id)
    {
      cerr << "Connection closed (id=" << connection_id << ")" << endl;

      clients.erase(connection_id);
    }
  );

  for (;;) {
    /* TODO: returns Poller::Result::Type::Exit sometimes? */
    server.loop_once();
  }

  return EXIT_SUCCESS;
}
