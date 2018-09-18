#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <fcntl.h>
#include <signal.h>

#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <random>
#include <algorithm>

#include <pqxx/pqxx>
#include "yaml-cpp/yaml.h"
#include "util.hh"
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

#ifdef NONSECURE
using WebSocketServer = WebSocketTCPServer;
#else
using WebSocketServer = WebSocketSecureServer;
#endif

/* global settings */
static const unsigned int DEFAULT_MAX_BUFFER_S = 15;
static const unsigned int DEFAULT_MAX_INFLIGHT_S = 10;
static const size_t DEFAULT_MAX_WS_FRAME_LEN = 100000;
static const size_t DEFAULT_MAX_WS_FRAME_NUM = 10;
static const size_t DEFAULT_MAX_WS_QUEUE_LEN = 30 * DEFAULT_MAX_WS_FRAME_LEN;
static const unsigned int MAX_IDLE_S = 10;  /* max client idle time (seconds) */

static unsigned int max_buffer_seconds;
static unsigned int max_inflight_seconds;
static size_t max_ws_frame_len;
static size_t max_ws_queue_len;

static map<string, Channel> channels;  /* key: channel name */
static map<uint64_t, WebSocketClient> clients;  /* key: connection ID */

static Timerfd global_timer;  /* non-blocking global timer fd for scheduling */

static fs::path log_dir;  /* parent directory for logging */
static map<string, FileDescriptor> log_fds;  /* map log name to fd */
static const unsigned int MAX_LOG_FILESIZE = 10 * 1024 * 1024;  /* 10 MB */

static string server_id;
static bool debug = false;

/* log data */
static unsigned int log_rebuffer_num = 0;
static time_t last_minute_written = 0;

void print_usage(const string & program_name)
{
  cerr << program_name << " <YAML configuration> <server ID> [debug]" << endl;
}

/* return "connection_id,username" or "connection_id," (unknown username) */
string client_signature(const uint64_t connection_id)
{
  const auto client_it = clients.find(connection_id);
  if (client_it != clients.end()) {
    return client_it->second.signature();
  } else {
    return to_string(connection_id) + ",";
  }
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
    if (not client.is_format_capable(vf)) continue;

    size_t chunk_size = get<1>(data_map.at(vf));
    if (chunk_size <= 0) continue;

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
    if (not client.is_format_capable(vf)) continue;

    size_t chunk_size = get<1>(data_map.at(vf));
    if (chunk_size <= 0 or chunk_size > max_serve_size) {
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
    if (chunk_size <= 0) continue;

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

    if (chunk_size <= 0 or chunk_size > max_serve_size) {
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

void append_to_log(const string & log_stem, const string & log_line)
{
  string log_name = log_stem + "." + server_id + ".log";
  string log_path = log_dir / log_name;

  /* find or create a file descriptor for the log */
  auto log_it = log_fds.find(log_name);
  if (log_it == log_fds.end()) {
    log_it = log_fds.emplace(log_name, FileDescriptor(CheckSystemCall(
        "open (" + log_path + ")",
        open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644)))).first;
  }

  /* append a line to log */
  FileDescriptor & fd = log_it->second;
  fd.write(log_line + " " + server_id + "\n");

  /* rotate log if filesize is too large */
  if (fd.curr_offset() > MAX_LOG_FILESIZE) {
    fs::rename(log_path, log_path + ".old");
    cerr << "Renamed " << log_path << " to " << log_path + ".old" << endl;

    /* create new fd before closing old one */
    FileDescriptor new_fd(CheckSystemCall(
        "open (" + log_path + ")",
        open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644)));
    fd.close();  /* reader is notified and safe to open new fd immediately */

    log_it->second = move(new_fd);
  }
}

void append_log_rebuffer_rate(uint64_t cur_time)
{
  double rebuffer_rate = 0;
  if (clients.size() > 0) {
    rebuffer_rate = (double) log_rebuffer_num / clients.size();
  }
  string log_line = to_string(cur_time) + " " + to_string(rebuffer_rate);
  append_to_log("rebuffer_rate", log_line);
}

void reinit_log_data()
{
  /* reinit rebuffer_rate */
  log_rebuffer_num = 0;
  for (const auto & client_pair : clients) {
    if (client_pair.second.rebuffer()) {
      log_rebuffer_num++;
    }
  }

  /* update corresponding logs */
  append_log_rebuffer_rate(time(nullptr));
}

void serve_video_to_client(WebSocketServer & server, WebSocketClient & client)
{
  Channel & channel = channels.at(client.channel().value());

  uint64_t next_vts = client.next_vts().value();
  double ssim;

  if (not client.next_vsegment()) { /* or try a lower quality */
    /* Start new chunk */
    if (not channel.vready(next_vts)) {
      return;
    }
    const VideoFormat & next_vq = select_video_quality(client);

    ssim = channel.vssim(next_vts).at(next_vq);
    cerr << client.signature() << ": channel " << channel.name()
         << ", video " << next_vts << " " << next_vq << " " << ssim << endl;

    optional<mmap_t> init_mmap;
    if (not client.curr_vq() or next_vq != client.curr_vq().value()) {
      init_mmap = channel.vinit(next_vq);
    }
    client.set_next_vsegment(next_vq, channel.vdata(next_vq, next_vts),
                             init_mmap);
  } else {
    ssim = channel.vssim(next_vts).at(client.next_vsegment().value().format());
    cerr << client.signature() << ": channel " << channel.name()
         << ", continuing video " << next_vts << endl;
  }

  for (size_t num = 0; num < DEFAULT_MAX_WS_FRAME_NUM; num++) {
    VideoSegment & next_vsegment = client.next_vsegment().value();

    ServerVideoMsg video_msg(channel.name(),
                             next_vsegment.format().to_string(),
                             ssim,
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
      return;
    }
  }
}

void serve_audio_to_client(WebSocketServer & server, WebSocketClient & client)
{
  Channel & channel = channels.at(client.channel().value());
  uint64_t next_ats = client.next_ats().value();

  if (not client.next_asegment()) { /* or try a lower quality */
    if (not channel.aready(next_ats)) {
      return;
    }

    const AudioFormat & next_aq = select_audio_quality(client);

    cerr << client.signature() << ": channel " << channel.name()
         << ", audio " << next_ats << " " << next_aq << endl;

    optional<mmap_t> init_mmap;
    if (not client.curr_aq() or next_aq != client.curr_aq().value()) {
      init_mmap = channel.ainit(next_aq);
    }
    client.set_next_asegment(next_aq, channel.adata(next_aq, next_ats),
                             init_mmap);
  } else {
    cerr << client.signature() << ": channel " << channel.name()
         << ", continuing audio " << next_ats << endl;
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
  if (not channel.init_vts()) {
    cerr << client.signature() << ": cannot reinit laggy client "
         << "(channel is not ready)" << endl;
    return;
  }

  uint64_t init_vts = channel.init_vts().value();
  uint64_t init_ats = channel.find_ats(init_vts);

  cerr << client.signature() << ": reinitialize laggy client "
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

  if (not channel.ready()) {
    cerr << client.signature()
         << ": cannot serve because channel is not available anymore" << endl;
    return;
  }

  if (channel.live()) {
    /* reinitialize very slow clients if the cleaner has caught up */
    auto vclean_frontier = channel.vclean_frontier();
    auto aclean_frontier = channel.aclean_frontier();
    if ((vclean_frontier and
         vclean_frontier.value() >= client.next_vts().value()) or
        (aclean_frontier and
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
  server.poller().add_action(Poller::Action(global_timer, Direction::In,
    [&server]()->Result {
      /* must read the timerfd, and check if timer has fired */
      if (global_timer.expirations() == 0) {
        return ResultType::Continue;
      }

      const auto curr_time = time(nullptr);

      /* iterate over all connections */
      set<uint64_t> connections_to_close;
      for (auto & client_pair : clients) {
        uint64_t connection_id = client_pair.first;
        WebSocketClient & client = client_pair.second;

        /* have not received messages from client for 10 seconds */
        const auto last_msg_time = client.get_last_msg_time();
        if (last_msg_time and curr_time - *last_msg_time > MAX_IDLE_S) {
          /* don't erase the client (in close callback) until after for loop */
          connections_to_close.emplace(connection_id);
          cerr << client.signature() << ": drop connection after "
               << MAX_IDLE_S << " seconds" << endl;
          continue;
        }

        if (client.channel()) {
          /* only serve clients from which server has received client-init */
          serve_client(server, client);
        }
      }

      /* clients can be safely erased now */
      for (const uint64_t connection_id : connections_to_close) {
        server.clean_idle_connection(connection_id);
      }

      /* write active_streams count to file once per minute */
      time_t this_minute_floor = curr_time - curr_time % 60;
      if (this_minute_floor > last_minute_written) {
        last_minute_written = this_minute_floor;

        for (const auto & name_channel_pair : channels) {
          auto channel = name_channel_pair.second;
          string log_line = to_string(this_minute_floor) + " " +
                            channel.name() + " " +
                            to_string(channel.viewer_count());
          append_to_log("active_streams", log_line);
        }
      }
      return ResultType::Continue;
    }
  ));

  /* this timer fires every 100 ms */
  global_timer.start(100, 100);
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
  /* don't resume a connection if client is requesting a different channel */
  if (client.channel() and client.channel().value() != channel.name()) {
    return false;
  }

  /* check if the requested timestamps are valid */
  if (not (msg.next_vts and channel.is_valid_vts(msg.next_vts.value()) and
           msg.next_ats and channel.is_valid_ats(msg.next_ats.value()))) {
    return false;
  }

  uint64_t requested_vts = msg.next_vts.value();
  uint64_t requested_ats = msg.next_ats.value();

  if (channel.live()) {
    /* live: don't try to resume if the requested video is already behind
     * the live edge */
    if (not channel.vlive_frontier() or
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

  cerr << client.signature() << ": connection resumed" << endl;
  return true;
}

void handle_client_init(WebSocketServer & server, WebSocketClient & client,
                        const ClientInitMsg & msg)
{
  /* ignore invalid channel request */
  auto it = channels.find(msg.channel);
  if (it == channels.end()) {
    cerr << client.signature() << ": requested channel "
         << msg.channel << " not found" << endl;
    return;
  }

  auto & channel = it->second;

  /* ignore client-init if the channel is not ready */
  if (not channel.ready()) {
    cerr << client.signature()
         << ": ignored client-init (channel is not ready)" << endl;
    return;
  }

  /* check if the streaming can be resumed */
  if (resume_connection(server, client, msg, channel)) {
    channel.set_viewer_count(channel.viewer_count() + 1);
    return;
  }

  uint64_t init_vts = channel.init_vts().value();
  uint64_t init_ats = channel.find_ats(init_vts);

  /* save old channel for use in updating view count */
  const string old_channel_string = client.channel() ?
                                    client.channel().value() : "";

  client.init(channel.name(), init_vts, init_ats);

  /* set client's browser and system info */
  client.set_browser(msg.browser);
  client.set_os(msg.os);

  /* set client's screen info */
  client.set_screen_height(msg.screen_height);
  client.set_screen_width(msg.screen_width);
  client.set_max_video_size(channel.vformats());

  /* reinit the log data */
  reinit_log_data();

  send_server_init(server, client, false /* initialize rather than resume */);

  /* increment/decrement viewer counts for new/old channel respectively */
  channel.set_viewer_count(channel.viewer_count() + 1);

  if (not old_channel_string.empty()) {
    Channel & old_channel = channels.at(old_channel_string);
    old_channel.set_viewer_count(old_channel.viewer_count() - 1);
  }

  cerr << client.signature() << ": connection initialized. " << msg.browser
       << " on " << msg.os << endl;
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

  /* reset the max video size in case that the screen size was changed */
  if (msg.screen_height != client.screen_height() or
      msg.screen_width != client.screen_width()) {
    client.set_screen_height(msg.screen_height);
    client.set_screen_width(msg.screen_width);

    /* throw an error if the client current channel doesn't exist in channels */
    try {
      if (client.channel()) {
        client.set_max_video_size(channels.at(client.channel().value()).vformats());
      }
    } catch (const out_of_range & e) {
      cerr << client.signature() << "the current channel doesn't exist: "
           << e.what() << endl;
    }
  }

  uint64_t cur_time = time(nullptr);

  /* only interested in logging the events below */
  if (msg.event == ClientInfoMsg::PlayerEvent::Timer or
      msg.event == ClientInfoMsg::PlayerEvent::Rebuffer or
      msg.event == ClientInfoMsg::PlayerEvent::CanPlay) {
    /* convert video playback buffer to string (%.1f) */
    double vbuf_len = min(max(msg.video_buffer_len, 0.0), 99.0);
    const size_t buf_size = 5;
    char buf[buf_size];
    int n = snprintf(buf, buf_size, "%.1f", vbuf_len);
    if (n < 0 or n >= static_cast<int>(buf_size)) {
      cerr << "Warning in recording video playback buffer: error occurred or "
           << "the converted string is truncated" << endl;
      return;
    }

    /* record playback buffer occupancy */
    string log_line = to_string(cur_time) + " " + client.username() + " "
        + *client.channel() + " " + to_string(static_cast<int>(msg.event))
        + " " + buf;
    append_to_log("playback_buffer", log_line);
  } else if (msg.event == ClientInfoMsg::PlayerEvent::VideoAck) {
    if (not msg.quality or not msg.timestamp or not msg.ssim) {
      cerr << "Received a VideoAck of invalid format" << endl;
      return;
    }

    /* record video quality on every VideoAck for the first video segment */
    if (msg.byte_offset == 0) {
      string log_line = to_string(cur_time) + " " + client.username() + " "
          + *client.channel() + " " + to_string(*msg.timestamp) + " "
          + *msg.quality + " " + to_string(*msg.ssim);
      append_to_log("video_quality", log_line);
    }
  }

  /* record rebuffer event and rebuffer rate */
  if (msg.event == ClientInfoMsg::PlayerEvent::Rebuffer or
      msg.event == ClientInfoMsg::PlayerEvent::CanPlay) {
    if (msg.event == ClientInfoMsg::PlayerEvent::Rebuffer) {
      string log_line = to_string(cur_time) + " " + client.username() + " "
                        + *client.channel();
      append_to_log("rebuffer_event", log_line);
    }

    if (not client.rebuffer() and
        msg.event == ClientInfoMsg::PlayerEvent::Rebuffer) {
      log_rebuffer_num++;
      client.set_rebuffer(true);
      append_log_rebuffer_rate(cur_time);
    }
    if (client.rebuffer() and
        msg.event == ClientInfoMsg::PlayerEvent::CanPlay) {
      log_rebuffer_num--;
      client.set_rebuffer(false);
      append_log_rebuffer_rate(cur_time);
    }
  }

  /* get current throughput (measured in kpbs) of the client */
  if (msg.receiving_time_ms and msg.received_bytes and
      msg.receiving_time_ms.value() > 0 and msg.receiving_time_ms.value() < 3000) {
    client.set_curr_tput((double) msg.received_bytes.value() * 8
                         / msg.receiving_time_ms.value());
  }
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

    try {
      auto ret = channels.emplace(
          piecewise_construct,
          forward_as_tuple(channel_name),
          forward_as_tuple(channel_name, config[channel_name], inotify));
      if (not ret.second) {
        cerr << "Warning: tried to create a duplicate channel "
             << channel_name << endl;
      }
    } catch (const exception & e) {
      cerr << "Error: exceptions in channel " << channel_name << ": "
           << e.what() << endl;
    }
  }
}

bool auth_client(const string & session_key, pqxx::nontransaction & db_work)
{
  try {
    pqxx::result r = db_work.prepared("auth")(session_key).exec();

    if (r.size() == 1 and r[0].size() == 1) {
      /* returned record is valid containing only true or false */
      return r[0][0].as<bool>();
    } else {
      cerr << "Authentication failed due to invalid returned record" << endl;
      return false;
    }
  } catch (const exception & e) {
    print_exception("auth_client", e);
    return false;
  }
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 3 and argc != 4) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (argc == 4 and string(argv[3]) == "debug") {
    debug = true;
  }

  /* ignore SIGPIPE generated by SSL_write */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    cerr << "signal: failed to ignore SIGPIPE" << endl;
    return EXIT_FAILURE;
  }

  server_id = argv[2];

  /* load YAML settings */
  YAML::Node config = YAML::LoadFile(argv[1]);
  load_global_settings(config);
  log_dir = config["log_dir"].as<string>();

  /* create a WebSocketServer instance */
  const string ip = "0.0.0.0";
  const uint16_t port = config["port"].as<uint16_t>();
  string congestion_control = "default";
  if (config["congestion_control"]) {
    congestion_control = config["congestion_control"].as<string>();
  }
  WebSocketServer server {{ip, port}, congestion_control};

  #ifdef NONSECURE
  cerr << "Launching non-secure WebSocket server" << endl;
  #else
  server.ssl_context().use_private_key_file(config["private_key"].as<string>());
  server.ssl_context().use_certificate_file(config["certificate"].as<string>());
  cerr << "Launching secure WebSocket server" << endl;
  #endif

  /* connect to database */
  string db_conn_str = config["db_connection"].as<string>();
  db_conn_str += " password=" + safe_getenv("PUFFER_PORTAL_DB_KEY");

  pqxx::connection db_conn(db_conn_str);
  cerr << "Connected to database: " << db_conn.hostname() << endl;

  /* reuse the same nontransaction */
  pqxx::nontransaction db_work(db_conn);

  /* prepare a statement to check if session_key is valid */
  db_conn.prepare("auth", "SELECT EXISTS(SELECT 1 FROM django_session WHERE "
    "session_key = $1 AND expire_date > now());");

  /* load channels and mmap (existing and new) media files */
  Inotify inotify(server.poller());
  load_channels(config, inotify);

  /* set server callbacks */
  server.set_message_callback(
    [&server, &db_work](const uint64_t connection_id, const WSMessage & msg)
    {
      try {
        if (debug) {
          cerr << connection_id << ": message " << msg.payload() << endl;
        }

        WebSocketClient & client = clients.at(connection_id);
        ClientMsgParser parser(msg.payload());
        client.set_last_msg_time(time(nullptr));

        if (parser.msg_type() == ClientMsgParser::Type::Init) {
          const ClientInitMsg & init_msg = parser.parse_init_msg();

          if (not client.is_authenticated()) {
            /* user authentication */
            if (not auth_client(init_msg.session_key, db_work)) {
              cerr << connection_id << ": authentication failed" << endl;
              server.close_connection(connection_id);
              return;
            }

            /* set username only if authentication succeeds */
            client.set_authed_username(init_msg.username);
          }

          client.set_ip(server.peer_addr(connection_id).ip());
          handle_client_init(server, client, init_msg);
        } else {
          /* parse a message other than client-init only if user is authed */
          if (not client.is_authenticated()) {
            cerr << connection_id << ": ignoring messages from a "
                 << "non-authenticated user" << endl;
            server.close_connection(connection_id);
            return;
          }

          if (parser.msg_type() ==  ClientMsgParser::Type::Info) {
            handle_client_info(client, parser.parse_info_msg());
          }
        }
      } catch (const exception & e) {
        cerr << client_signature(connection_id)
             << ": warning in message callback: " << e.what() << endl;
        server.close_connection(connection_id);
      }
    }
  );

  server.set_open_callback(
    [&server](const uint64_t connection_id)
    {
      try {
        cerr << connection_id << ": connection opened" << endl;

        clients.emplace(piecewise_construct,
                        forward_as_tuple(connection_id),
                        forward_as_tuple(connection_id)); /* WebSocketClient */
      } catch (const exception & e) {
        cerr << client_signature(connection_id)
             << ": warning in open callback: " << e.what() << endl;
        server.close_connection(connection_id);
      }
    }
  );

  server.set_close_callback(
    [](const uint64_t connection_id)
    {
      try {
        cerr << connection_id << ": connection closed" << endl;

        clients.erase(connection_id);
        reinit_log_data();
      } catch (const exception & e) {
        cerr << client_signature(connection_id)
             << ": warning in close callback: " << e.what() << endl;
      }
    }
  );

  /* start a global timer to serve media to clients */
  start_global_timer(server);
  return server.loop();
}
