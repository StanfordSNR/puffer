#include <cstdint>
#include <cmath>

#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <random>
#include <algorithm>

#include "media_formats.hh"
#include "inotify.hh"
#include "timerfd.hh"
#include "channel.hh"
#include "server_message.hh"
#include "client_message.hh"
#include "ws_server.hh"
#include "ws_client.hh"

using namespace std;
using namespace PollerShortNames;
using WebSocketServer = WebSocketTCPServer;
// using WebSocketServer = WebSocketSecureServer;

/* global settings */
static const int DEFAULT_MAX_BUFFER_S = 60;
static const int DEFAULT_MAX_INFLIGHT_S = 5;
static const size_t DEFAULT_MAX_WS_FRAME_LEN = 100000;
static const size_t DEFAULT_MAX_WS_QUEUE_LEN = DEFAULT_MAX_WS_FRAME_LEN;

static unsigned int max_buffer_seconds;
static unsigned int max_inflight_seconds;
static size_t max_ws_frame_len;
static size_t max_ws_queue_len;

static map<string, Channel> channels;  /* key: channel name */

static map<uint64_t, WebSocketClient> clients;  /* key: connection ID */

static Timerfd global_timer;  /* non-blocking global timer fd for scheduling */

static bool debug = false;

void print_usage(const string & program_name)
{
  cerr << program_name << " <YAML configuration> [debug]" << endl;
}

const VideoFormat & select_video_quality(WebSocketClient & client)
{
  // TODO: make a better choice
  Channel & channel = channels.at(client.channel().value());

  /* simple buffer-based algorithm: assume max buffer is 10 seconds */
  double buf = min(max(client.video_playback_buf(), 0.0), 10.0);

  uint64_t next_vts = client.next_vts().value();
  const auto & data_map = channel.vdata(next_vts);
  const auto & ssim_map = channel.vssim(next_vts);

  /* get max and min chunk size for the next video ts */
  size_t max_size = 0, min_size = SIZE_MAX;

  size_t MAX_VFORMATS = channel.vformats().size();
  size_t max_idx = MAX_VFORMATS, min_idx = MAX_VFORMATS;

  for (size_t i = 0; i < channel.vformats().size(); i++) {
    const auto & vf = channel.vformats()[i];
    size_t chunk_size = get<1>(data_map.at(vf));

    if (chunk_size > max_size) {
      max_size = chunk_size;
      max_idx = i;
    }

    if (chunk_size < min_size) {
      min_size = chunk_size;
      min_idx = i;
    }
  }

  assert(max_idx < MAX_VFORMATS);
  assert(min_idx < MAX_VFORMATS);

  if (buf >= 8.0) {
    return channel.vformats()[max_idx];
  } else if (buf <= 2.0) {
    return channel.vformats()[min_idx];
  }

  /* pick the chunk with highest SSIM but with size <= max_serve_size */
  double max_serve_size = ceil(buf * (max_size - min_size) / 10.0 + min_size);
  double highest_ssim = 0.0;
  size_t ret_idx = MAX_VFORMATS;

  for (size_t i = 0; i < channel.vformats().size(); i++) {
    const auto & vf = channel.vformats()[i];
    size_t chunk_size = get<1>(data_map.at(vf));

    if (chunk_size > max_serve_size) {
      continue;
    }

    double ssim = ssim_map.at(vf);
    if (ssim > highest_ssim) {
      highest_ssim = ssim;
      ret_idx = i;
    }
  }

  assert(ret_idx < MAX_VFORMATS);
  return channel.vformats()[ret_idx];
}

const AudioFormat & select_audio_quality(WebSocketClient & client)
{
  // TODO: make a better choice
  Channel & channel = channels.at(client.channel().value());

  /* simple buffer-based algorithm: assume max buffer is 10 seconds */
  double buf = min(max(client.audio_playback_buf(), 0.0), 10.0);

  uint64_t next_ats = client.next_ats().value();
  const auto & data_map = channel.adata(next_ats);

  /* get max and min chunk size for the next video ts */
  size_t max_size = 0, min_size = SIZE_MAX;

  size_t MAX_AFORMATS = channel.aformats().size();
  size_t max_idx = MAX_AFORMATS, min_idx = MAX_AFORMATS;

  for (size_t i = 0; i < channel.aformats().size(); i++) {
    const auto & af = channel.aformats()[i];
    size_t chunk_size = get<1>(data_map.at(af));

    if (chunk_size > max_size) {
      max_size = chunk_size;
      max_idx = i;
    }

    if (chunk_size < min_size) {
      min_size = chunk_size;
      min_idx = i;
    }
  }

  assert(max_idx < MAX_AFORMATS);
  assert(min_idx < MAX_AFORMATS);

  if (buf >= 8.0) {
    return channel.aformats()[max_idx];
  } else if (buf <= 2.0) {
    return channel.aformats()[min_idx];
  }

  /* pick the chunk with biggest size <= max_serve_size */
  double max_serve_size = ceil(buf * (max_size - min_size) / 10.0 + min_size);
  size_t biggest_chunk_size = 0;
  size_t ret_idx = MAX_AFORMATS;

  for (size_t i = 0; i < channel.aformats().size(); i++) {
    const auto & af = channel.aformats()[i];
    size_t chunk_size = get<1>(data_map.at(af));

    if (chunk_size > max_serve_size) {
      continue;
    }

    if (chunk_size > biggest_chunk_size) {
      biggest_chunk_size = chunk_size;
      ret_idx = i;
    }
  }

  assert(ret_idx < MAX_AFORMATS);
  return channel.aformats()[ret_idx];
}

void serve_video_to_client(WebSocketServer & server, WebSocketClient & client)
{
  Channel & channel = channels.at(client.channel().value());

  uint64_t next_vts = client.next_vts().value();

  if (not client.next_vsegment().has_value()) { /* or try a lower quality */
    /* Start new chunk */
    if (not channel.vready(next_vts)) {
      return;
    }
    const VideoFormat & next_vq = select_video_quality(client);

    cerr << client.connection_id() << ": serving channel " << channel.name()
         << ", video " << next_vts << " " << next_vq << endl;

    optional<mmap_t> init_mmap;
    if (not client.curr_vq().has_value() or
        next_vq != client.curr_vq().value()) {
      init_mmap = channel.vinit(next_vq);
    }
    client.set_next_vsegment(next_vq, channel.vdata(next_vq, next_vts),
                             init_mmap);
  } else {
    cerr << client.connection_id() << ": continuing video "
         << next_vts << endl;
  }

  VideoSegment & next_vsegment = client.next_vsegment().value();

  ServerVideoMsg video_msg(channel.name(),
                           next_vsegment.format().to_string(),
                           next_vts,
                           channel.vduration(),
                           next_vsegment.offset(),
                           next_vsegment.length());
  string frame_payload = video_msg.to_string();
  next_vsegment.read(frame_payload, max_ws_frame_len);

  WSFrame frame {true, WSFrame::OpCode::Binary, move(frame_payload)};
  server.queue_frame(client.connection_id(), frame);

  if (next_vsegment.done()) {
    client.set_next_vts(next_vts + channel.vduration());
    client.set_curr_vq(next_vsegment.format());
    client.clear_next_vsegment();
  }
}

void serve_audio_to_client(WebSocketServer & server, WebSocketClient & client)
{
  Channel & channel = channels.at(client.channel().value());
  uint64_t next_ats = client.next_ats().value();

  if (not client.next_asegment().has_value()) { /* or try a lower quality */
    if (not channel.aready(next_ats)) {
      return;
    }

    const AudioFormat & next_aq = select_audio_quality(client);

    cerr << client.connection_id() << ": serving channel " << channel.name()
         << ", audio " << next_ats << " " << next_aq << endl;

    optional<mmap_t> init_mmap;
    if (not client.curr_aq().has_value() or
        next_aq != client.curr_aq().value()) {
      init_mmap = channel.ainit(next_aq);
    }
    client.set_next_asegment(next_aq, channel.adata(next_aq, next_ats),
                             init_mmap);
  } else {
    cerr << client.connection_id() << ": continuing audio "
         << next_ats << endl;
  }

  AudioSegment & next_asegment = client.next_asegment().value();

  ServerAudioMsg audio_msg(channel.name(),
                           next_asegment.format().to_string(),
                           next_ats,
                           channel.aduration(),
                           next_asegment.offset(),
                           next_asegment.length());
  string frame_payload = audio_msg.to_string();
  next_asegment.read(frame_payload, max_ws_frame_len);

  WSFrame frame {true, WSFrame::OpCode::Binary, move(frame_payload)};
  server.queue_frame(client.connection_id(), frame);

  if (next_asegment.done()) {
    client.set_next_ats(next_ats + channel.aduration());
    client.set_curr_aq(next_asegment.format());
    client.clear_next_asegment();
  }
}

inline unsigned int video_in_flight(const Channel & channel,
                                    const WebSocketClient & client)
{
  /* Return number of seconds of video in flight */
  return (client.next_vts().value() - client.client_next_vts().value())
          / channel.timescale();
}

inline unsigned int audio_in_flight(const Channel & channel,
                                    const WebSocketClient & client)
{
  /* Return number of seconds of audio in flight */
  return (client.next_ats().value() - client.client_next_ats().value())
          / channel.timescale();
}

void reinit_laggy_client(WebSocketServer & server, WebSocketClient & client,
                         const Channel & channel)
{
  /* return if the channel is not ready */
  if (not channel.init_vts().has_value()) {
    cerr << client.connection_id() << ": cannot reinit laggy client "
         << "(channel is not ready)" << endl;
    return;
  }

  uint64_t init_vts = channel.init_vts().value();
  uint64_t init_ats = channel.find_ats(init_vts);

  cerr << client.connection_id() << ": reinitialize laggy client "
       << client.next_vts().value() << "->" << init_vts << endl;
  client.init(channel.name(), init_vts, init_ats);

  ServerInitMsg reinit(channel.name(), channel.vcodec(),
                       channel.acodec(), channel.timescale(),
                       client.next_vts().value(),
                       client.next_ats().value(),
                       client.init_id(), false);
  WSFrame frame {true, WSFrame::OpCode::Binary, reinit.to_string()};
  server.queue_frame(client.connection_id(), frame);
}

void serve_client(WebSocketServer & server, WebSocketClient & client)
{
  const Channel & channel = channels.at(client.channel().value());

  if (channel.live()) {
    /* reinitialize very slow clients if the cleaner has caught up */
    auto vclean_frontier = channel.vclean_frontier();
    auto aclean_frontier = channel.aclean_frontier();
    if ((vclean_frontier.has_value() and
         vclean_frontier.value() >= client.next_vts().value()) or
        (aclean_frontier.has_value() and
         aclean_frontier.value() >= client.next_ats().value())) {
      reinit_laggy_client(server, client, channel);
      return;
    }
  }

  /* return if the server's queue has been full */
  if (server.buffer_bytes(client.connection_id()) >= max_ws_queue_len) {
    return;
  }

  const bool can_send_video =
      client.video_playback_buf() < max_buffer_seconds and
      video_in_flight(channel, client) < max_inflight_seconds;
  const bool can_send_audio =
      client.audio_playback_buf() < max_buffer_seconds and
      audio_in_flight(channel, client) < max_inflight_seconds;

  if (client.next_vts().value() > client.next_ats().value()) {
    /* prioritize audio */
    if (can_send_audio) {
      serve_audio_to_client(server, client);
    }
    /* serve video only if there is still room */
    if (can_send_video and
        server.buffer_bytes(client.connection_id()) < max_ws_queue_len) {
      serve_video_to_client(server, client);
    }
  } else {
    /* prioritize video */
    if (can_send_video) {
      serve_video_to_client(server, client);
    }
    /* serve audio only if there is still room */
    if (can_send_audio and
        server.buffer_bytes(client.connection_id()) < max_ws_queue_len) {
      serve_audio_to_client(server, client);
    }
  }
}

void start_global_timer(WebSocketServer & server)
{
  /* the timer fires every 10 ms */
  global_timer.start(10, 10);

  server.poller().add_action(
    Poller::Action(global_timer, Direction::In,
      [&server]() {
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

void send_server_init(WebSocketServer & server, WebSocketClient & client,
                      const bool can_resume)
{
  const Channel & channel = channels.at(client.channel().value());

  ServerInitMsg init(channel.name(), channel.vcodec(),
                     channel.acodec(), channel.timescale(),
                     client.next_vts().value(),
                     client.next_ats().value(),
                     client.init_id(), can_resume);
  WSFrame frame {true, WSFrame::OpCode::Binary, init.to_string()};

  /* drop previously queued frames before sending server init */
  server.clear_buffer(client.connection_id());
  server.queue_frame(client.connection_id(), frame);
}

bool resume_connection(WebSocketServer & server, WebSocketClient & client,
                       const ClientInitMsg & msg, const Channel & channel)
{
  /* resume a connection only if client was playing the same channel */
  if (not client.channel().has_value() or
      client.channel().value() != channel.name()) {
    return false;
  }

  /* check if the requested timestamps are valid */
  if (not (msg.next_vts.has_value() and
           channel.is_valid_vts(msg.next_vts.value()) and
           msg.next_ats.has_value() and
           channel.is_valid_ats(msg.next_ats.value()))) {
    return false;
  }

  uint64_t requested_vts = msg.next_vts.value();
  uint64_t requested_ats = msg.next_ats.value();

  if (channel.live()) {
    /* live: don't try to resume if the requested video is already behind
     * the live edge */
    if (not channel.vlive_frontier().has_value() or
        requested_vts < channel.vlive_frontier().value()) {
      return false;
    }

    /* check if the _previous_ chunks of the requestd video/audio are ready */
    if (not (channel.vready(requested_vts - channel.vduration()) and
             channel.aready(requested_ats - channel.aduration()))) {
      return false;
    }
  } else {
    /* VoD: check if the requested video and audio are ready */
    if (not (channel.vready(requested_vts) and
             channel.aready(requested_ats))) {
      return false;
    }
  }

  /* reinitialize the client */
  client.init(channel.name(), requested_vts, requested_ats);
  send_server_init(server, client, true /* can resume */);

  cerr << client.connection_id() << ": connection resumed" << endl;
  return true;
}

void handle_client_init(WebSocketServer & server, WebSocketClient & client,
                        const ClientInitMsg & msg)
{
  /* ignore invalid channel request */
  auto it = channels.find(msg.channel);
  if (it == channels.end()) {
    cerr << client.connection_id() << ": requested channel not found" << endl;
    return;
  }

  const auto & channel = it->second;

  /* ignore client-init if the channel is not ready */
  if (not channel.init_vts().has_value()) {
    cerr << client.connection_id()
         << ": ignored client-init (channel is not ready)" << endl;
    return;
  }

  /* check if the streaming can be resumed */
  if (resume_connection(server, client, msg, channel)) {
    return;
  }

  uint64_t init_vts = channel.init_vts().value();
  uint64_t init_ats = channel.find_ats(init_vts);

  client.init(channel.name(), init_vts, init_ats);

  send_server_init(server, client, false /* initialize rather than resume */);

  cerr << client.connection_id() << ": connection initialized" << endl;
}

void handle_client_info(WebSocketClient & client, const ClientInfoMsg & msg)
{
  /* ignore client messages with old ID (init_id) */
  if (msg.init_id != client.init_id()) {
    return;
  }

  client.set_audio_playback_buf(msg.audio_buffer_len);
  client.set_video_playback_buf(msg.video_buffer_len);
  client.set_client_next_vts(msg.next_video_timestamp);
  client.set_client_next_ats(msg.next_audio_timestamp);
}

void load_global_settings(const YAML::Node & config)
{
  max_buffer_seconds = config["max_buffer_s"] ?
    config["max_buffer_s"].as<unsigned int>() : DEFAULT_MAX_BUFFER_S;
  max_inflight_seconds = config["max_inflight_s"] ?
    config["max_inflight_s"].as<unsigned int>() : DEFAULT_MAX_INFLIGHT_S;
  max_ws_frame_len = config["max_ws_frame_b"] ?
    config["max_ws_frame_b"].as<size_t>() : DEFAULT_MAX_WS_FRAME_LEN;
  max_ws_queue_len = config["max_ws_queue_b"] ?
    config["max_ws_queue_b"].as<size_t>() : DEFAULT_MAX_WS_QUEUE_LEN;
}

void load_channels(const YAML::Node & config, Inotify & inotify)
{
  /* load channels */
  for (YAML::const_iterator it = config["channel"].begin();
       it != config["channel"].end(); ++it) {
    const string & channel_name = it->as<string>();

    if (not config[channel_name]) {
      throw runtime_error("Cannot find details of channel: " + channel_name);
    }

    auto ret = channels.emplace(
        piecewise_construct,
        forward_as_tuple(channel_name),
        forward_as_tuple(channel_name, config[channel_name], inotify));
    if (not ret.second) {
      throw runtime_error("Duplicate channels found: " + channel_name);
    }
  }
}

void close_connection(WebSocketServer & server, const uint64_t connection_id)
{
  try {
    server.close_connection(connection_id);
  } catch (const exception & e) {
    cerr << "Warning: cannot close the connection " << connection_id << endl;
  }

  clients.erase(connection_id);
}

bool auth_client(const ClientInitMsg & msg)
{
  cerr << msg.session_key << endl;
  return true;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2 and argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (argc == 3 and string(argv[2]) == "debug") {
    debug = true;
  }

  /* load YAML settings */
  YAML::Node config = YAML::LoadFile(argv[1]);
  load_global_settings(config);

  /* create a WebSocketServer instance */
  const string ip = "0.0.0.0";
  const uint16_t port = config["port"].as<uint16_t>();
  WebSocketServer server {{ip, port}};
  // server.ssl_context().use_private_key_file(config["private_key"].as<string>());
  // server.ssl_context().use_certificate_file(config["certificate"].as<string>());

  /* load channels and mmap (existing and new) media files */
  Inotify inotify(server.poller());
  load_channels(config, inotify);

  /* set server callbacks */
  server.set_message_callback(
    [&server](const uint64_t connection_id, const WSMessage & msg)
    {
      if (debug) {
        cerr << connection_id << ": message " << msg.payload() << endl;
      }

      try {
        WebSocketClient & client = clients.at(connection_id);
        ClientMsgParser parser(msg.payload());

        switch (parser.msg_type()) {
        case ClientMsgParser::Type::Init:
          {
            const ClientInitMsg & client_init_msg = parser.parse_init_msg();

            if (not auth_client(client_init_msg)) {
              cerr << connection_id << ": authentication failed" << endl;
              close_connection(server, connection_id);
              break;
            }

            handle_client_init(server, client, client_init_msg);
          }
          break;
        case ClientMsgParser::Type::Info:
          handle_client_info(client, parser.parse_info_msg());
          break;
        default:
          break;
        }
      } catch (const exception & e) {
        cerr << "Warning in message callback: exception in client "
             << connection_id << ": " << e.what() << endl;
        close_connection(server, connection_id);
      }
    }
  );

  server.set_open_callback(
    [&server](const uint64_t connection_id)
    {
      cerr << connection_id << ": connection opened" << endl;
      clients.emplace(piecewise_construct,
                      forward_as_tuple(connection_id),
                      forward_as_tuple(connection_id)); /* WebSocketClient */
    }
  );

  server.set_close_callback(
    [](const uint64_t connection_id)
    {
      cerr << connection_id << ": connection closed" << endl;
      clients.erase(connection_id);
    }
  );

  /* start the global timer */
  start_global_timer(server);

  return server.loop();
}
