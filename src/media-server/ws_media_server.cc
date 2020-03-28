#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fcntl.h>
#include <signal.h>

#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <random>
#include <algorithm>
#include <pqxx/pqxx>

#include "util.hh"
#include "strict_conversions.hh"
#include "timestamp.hh"
#include "inotify.hh"
#include "timerfd.hh"
#include "channel.hh"
#include "server_message.hh"
#include "client_message.hh"
#include "ws_server.hh"
#include "ws_client.hh"
#include "media_formats.hh"
#include "yaml.hh"
#include "abr_algo.hh"

using namespace std;
using namespace PollerShortNames;

#ifdef NONSECURE
using WebSocketServer = WebSocketTCPServer;
#else
using WebSocketServer = WebSocketSecureServer;
#endif

/* global variables */
YAML::Node config;
static map<string, shared_ptr<Channel>> channels;  /* key: channel name */
static map<uint64_t, WebSocketClient> clients;  /* key: connection ID */

static const size_t MAX_WS_FRAME_B = 100 * 1024;  /* 10 KB */
static const unsigned int MAX_IDLE_MS = 60000; /* clean idle connections */

static const unsigned int MAX_CONNECTION_NUM = 10; /* max connections */

/* for logging */
static bool enable_logging = false;
static fs::path log_dir;  /* base directory for logging */
static string server_id;
static string expt_id;
static map<string, FileDescriptor> log_fds;  /* map log name to fd */
static const unsigned int MAX_LOG_FILESIZE = 100 * 1024 * 1024;  /* 100 MB */
static uint64_t last_minute = 0;  /* in ms; multiple of 60000 */

void print_usage(const string & program_name)
{
  cerr <<
  program_name << " <YAML configuration> <server ID> [<expt ID>]"
  << endl;
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

void append_to_log(const string & log_stem, const string & log_line)
{
  if (not enable_logging) {
    throw runtime_error("append_to_log: enable_logging must be true");
  }

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
  fd.write(log_line + "\n");

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

void serve_video_to_client(WebSocketServer & server,
                           WebSocketClient & client)
{
  const auto channel = client.channel();
  uint64_t next_vts = client.next_vts().value();

  /* save TCP info before client.select_video_format() */
  TCPInfo tcpi = server.get_tcp_info(client.connection_id());
  client.set_tcp_info(tcpi);

  /* select a video format using ABR algorithm */
  const VideoFormat & next_vformat = client.select_video_format();
  double ssim = channel->vssim(next_vts).at(next_vformat);

  /* check if a new init segment is needed */
  optional<mmap_t> init_mmap;
  if (not client.curr_vformat() or
      next_vformat != *client.curr_vformat()) {
    init_mmap = channel->vinit(next_vformat);
  }

  /* construct the next segment to send */
  const auto data_mmap = channel->vdata(next_vformat, next_vts);
  VideoSegment next_vsegment {next_vformat, data_mmap, init_mmap};

  /* divide the next segment into WebSocket frames and send */
  while (not next_vsegment.done()) {
    ServerVideoMsg video_msg(client.init_id().value(),
                             channel->name(),
                             next_vformat.to_string(),
                             next_vts,
                             next_vsegment.offset(),
                             next_vsegment.length(),
                             ssim);
    string frame_payload = video_msg.to_string();
    next_vsegment.read(frame_payload, MAX_WS_FRAME_B - frame_payload.size());

    WSFrame frame {true, WSFrame::OpCode::Binary, move(frame_payload)};
    server.queue_frame(client.connection_id(), frame);
  }

  /* finish sending */
  client.set_next_vts(next_vts + channel->vduration());
  client.set_curr_vformat(next_vformat);
  client.set_last_video_send_ts(timestamp_ms());

  cerr << client.signature() << ": channel " << channel->name()
       << ", video " << next_vts << " " << next_vformat << " " << ssim << endl;

  if (enable_logging) {
    string log_line = to_string(timestamp_ms()) + "," + channel->name() + ","
      + server_id + "," + expt_id + "," + client.username() + ","
      + to_string(client.first_init_id().value()) + ","
      + to_string(client.init_id().value()) + ","
      + to_string(next_vts) + ","
      + next_vformat.to_string() + ","
      + to_string(get<1>(data_mmap)) + "," + to_string(ssim)
      + "," + to_string(tcpi.cwnd) + "," + to_string(tcpi.in_flight) + ","
      + to_string(tcpi.min_rtt) + "," + to_string(tcpi.rtt) + ","
      + to_string(tcpi.delivery_rate) + ","
      + double_to_string(client.video_playback_buf(), 3) + ","
      + double_to_string(client.cum_rebuffer(), 3);
    append_to_log("video_sent", log_line);
  }
}

void serve_audio_to_client(WebSocketServer & server,
                           WebSocketClient & client)
{
  const auto channel = client.channel();
  uint64_t next_ats = client.next_ats().value();

  /* select an audio format using ABR algorithm */
  const AudioFormat & next_aformat = client.select_audio_format();

  /* check if a new init segment is needed */
  optional<mmap_t> init_mmap;
  if (not client.curr_aformat() or
      next_aformat != *client.curr_aformat()) {
    init_mmap = channel->ainit(next_aformat);
  }

  /* construct the next segment to send */
  const auto data_mmap = channel->adata(next_aformat, next_ats);
  AudioSegment next_asegment {next_aformat, data_mmap, init_mmap};

  /* divide the next segment into WebSocket frames and send */
  while (not next_asegment.done()) {
    ServerAudioMsg audio_msg(client.init_id().value(),
                             channel->name(),
                             next_aformat.to_string(),
                             next_ats,
                             next_asegment.offset(),
                             next_asegment.length());
    string frame_payload = audio_msg.to_string();
    next_asegment.read(frame_payload, MAX_WS_FRAME_B - frame_payload.size());

    WSFrame frame {true, WSFrame::OpCode::Binary, move(frame_payload)};
    server.queue_frame(client.connection_id(), frame);
  }

  /* finish sending */
  client.set_next_ats(next_ats + channel->aduration());
  client.set_curr_aformat(next_aformat);

  cerr << client.signature() << ": channel " << channel->name()
       << ", audio " << next_ats << " " << next_aformat << endl;
}

void send_server_init(WebSocketServer & server, WebSocketClient & client,
                      const bool can_resume)
{
  const auto channel = client.channel();

  ServerInitMsg init(client.init_id().value(), channel->name(),
                     channel->vcodec(), channel->acodec(),
                     channel->timescale(),
                     channel->vduration(), channel->aduration(),
                     client.next_vts().value(), client.next_ats().value(),
                     can_resume);
  WSFrame frame {true, WSFrame::OpCode::Binary, init.to_string()};

  /* drop previously queued frames before sending server-init */
  server.clear_buffer(client.connection_id());

  server.queue_frame(client.connection_id(), frame);
}

void send_server_error(WebSocketServer & server, WebSocketClient & client,
                       const ServerErrorMsg::Type error_type)
{
  ServerErrorMsg err_msg(client.init_id() ? *client.init_id() : 0, error_type);
  WSFrame frame {true, WSFrame::OpCode::Binary, err_msg.to_string()};

  server.queue_frame(client.connection_id(), frame);

  /* reset the client and wait for client-init */
  client.reset_channel();
}

void serve_client(WebSocketServer & server, WebSocketClient & client)
{
  if (not client.is_channel_initialized()) {
    return;
  }

  const auto channel = client.channel();

  /* notify the client that the requested channel is not available */
  if (not channel->ready_to_serve()) {
    send_server_error(server, client, ServerErrorMsg::Type::Unavailable);
    cerr << client.signature() << ": requested channel "
         << channel->name() << " is not available" << endl;
    return;
  }

  uint64_t next_vts = client.next_vts().value();
  uint64_t next_ats = client.next_ats().value();

  if (channel->live()) {
    /* reinit client if clean frontiers of live streaming have caught up */
    if ((channel->vclean_frontier() and
         next_vts <= *channel->vclean_frontier()) or
        (channel->aclean_frontier() and
         next_ats <= *channel->aclean_frontier())) {
      send_server_error(server, client, ServerErrorMsg::Type::Reinit);
      cerr << client.signature() << ": reinitialize laggy client" << endl;
      return;
    }
  } else {
    /* reinit client if a non-live channel is set to repeat playing */
    if (channel->repeat()) {
      if (next_vts > channel->vready_frontier() or
          next_ats > channel->aready_frontier()) {
        send_server_error(server, client, ServerErrorMsg::Type::Reinit);
        cerr << client.signature() << ": reinitialize client intentionally "
             << "as 'repeat' is set to true" << endl;
        return;
      }
    }
  }

  if (client.audio_playback_buf() <= WebSocketClient::MAX_BUFFER_S and
      client.audio_in_flight().value() == 0 and channel->aready_to_serve(next_ats)
      and next_ats <= next_vts) {
    serve_audio_to_client(server, client);
  }

  if (client.video_playback_buf() <= WebSocketClient::MAX_BUFFER_S and
      client.video_in_flight().value() == 0 and channel->vready_to_serve(next_vts)) {
    serve_video_to_client(server, client);
  }
}

void log_active_streams(const uint64_t this_minute)
{
  assert(enable_logging);

  /* channel name -> count */
  map<string, unsigned int> active_streams_count;

  for (const auto & client_pair : clients) {
    const auto channel = client_pair.second.channel();

    if (channel) {
      string channel_name = channel->name();

      auto map_it = active_streams_count.find(channel_name);
      if (map_it == active_streams_count.end()) {
        active_streams_count.emplace(channel_name, 1);
      } else {
        map_it->second += 1;
      }
    }
  }

  for (const auto & [channel_name, count] : active_streams_count) {
    string log_line = to_string(this_minute) + "," + channel_name + ","
      + server_id + "," + expt_id + "," + to_string(count);
    append_to_log("active_streams", log_line);
  }
}

void log_server_info(const uint64_t this_minute)
{
  /* the tag "server_id" is used to avoid data point overwriting;
   * the field "server_id" is used to count distinct values, i.e., the number
   * of running servers, as a workaround until InfluxDB supports DISTINCT
   * function to operate on tags */
  string log_line = to_string(this_minute) + "," + server_id + "," + server_id;
  append_to_log("server_info", log_line);
}

void start_slow_timer(Timerfd & slow_timer, WebSocketServer & server)
{
  bool enforce_moving_live_edge = false;
  if (config["enforce_moving_live_edge"]) {
    enforce_moving_live_edge = config["enforce_moving_live_edge"].as<bool>();
  }

  server.poller().add_action(Poller::Action(slow_timer, Direction::In,
    [&slow_timer, &server, enforce_moving_live_edge]()->Result {
      /* must read the timerfd, and check if timer has fired */
      if (slow_timer.expirations() == 0) {
        return ResultType::Continue;
      }

      /* mark channel as not available if live edge not advanced for a while */
      if (enforce_moving_live_edge) {
        for (const auto & channel_it : channels) {
          channel_it.second->enforce_moving_live_edge();
        }
      }

      set<uint64_t> connections_to_clean;

      for (auto & [connection_id, client] : clients) {
        /* have not received messages from client for a while */
        const auto elapsed = timestamp_ms() - client.last_msg_recv_ts();

        if (elapsed > MAX_IDLE_MS) {
          connections_to_clean.emplace(connection_id);
          cerr << client.signature() << ": cleaned idle connection" << endl;
          continue;
        }
      }

      /* connections can be safely cleaned now */
      for (const uint64_t connection_id : connections_to_clean) {
        clients.erase(connection_id);
        server.clean_idle_connection(connection_id);
      }

      if (enable_logging) {
        /* perform some tasks once per minute */
        const auto curr_time_s = timestamp_s();
        const auto this_minute = (curr_time_s - curr_time_s % 60) * 1000;

        if (last_minute == 0) {
          last_minute = this_minute;
        } else if (this_minute > last_minute) {
          /* server info: server heartbeats, etc. */
          log_server_info(this_minute);

          /* write active_streams count to file */
          log_active_streams(this_minute);

          last_minute = this_minute;
        }
      }

      return ResultType::Continue;
    }
  ));
}

bool resume_connection(WebSocketServer & server,
                       WebSocketClient & client,
                       const ClientInitMsg & msg,
                       const shared_ptr<Channel> & channel)
{
  /* check if requested timestamps exist */
  if (not msg.next_vts or not msg.next_ats) {
    return false;
  }

  uint64_t requested_vts = *msg.next_vts;
  uint64_t requested_ats = *msg.next_ats;

  /* check if the requested timestamps are ready to serve */
  if (not channel->vready_to_serve(requested_vts) or
      not channel->aready_to_serve(requested_ats)) {
    return false;
  }

  /* reinitialize the client */
  client.init_channel(channel, requested_vts, requested_ats);
  send_server_init(server, client, true /* can resume */);

  cerr << client.signature() << ": connection resumed" << endl;
  return true;
}

void handle_client_init(WebSocketServer & server, WebSocketClient & client,
                        const ClientInitMsg & msg)
{
  /* always set client's init_id when a client-init is received */
  client.set_init_id(msg.init_id);

  /* invalid channel request */
  auto it = channels.find(msg.channel);
  if (it == channels.end()) {
    send_server_error(server, client, ServerErrorMsg::Type::Unavailable);
    cerr << client.signature() << ": requested channel "
         << msg.channel << " is not found" << endl;
    return;
  }

  const auto channel = it->second;

  /* reply that the channel is not ready */
  if (not channel->ready_to_serve()) {
    send_server_error(server, client, ServerErrorMsg::Type::Unavailable);
    cerr << client.signature() << ": requested channel "
         << msg.channel << " is not ready" << endl;
    return;
  }

  /* record client-init */
  if (enable_logging) {
    string log_line = to_string(timestamp_ms()) + "," + msg.channel
      + "," + server_id + ",init," + expt_id + "," + client.username() + ","
      + to_string(client.first_init_id().value()) + ","
      + to_string(msg.init_id) + ",0,0" /* buffer cum_rebuf */;
    append_to_log("client_buffer", log_line);

    /* record system information */
    log_line = to_string(timestamp_ms()) + "," + server_id + ","
      + expt_id + "," + client.username() + ","
      + to_string(client.first_init_id().value()) + ","
      + to_string(msg.init_id) + ","
      + client.address().ip() + ","
      + client.os() + "," + client.browser() + ","
      + to_string(client.screen_width()) + ","
      + to_string(client.screen_height());
    append_to_log("client_sysinfo", log_line);
  }

  /* check if the streaming can be resumed */
  if (resume_connection(server, client, msg, channel)) {
    return;
  }

  uint64_t init_vts = channel->init_vts().value();
  uint64_t init_ats = channel->init_ats().value();

  client.init_channel(channel, init_vts, init_ats);
  send_server_init(server, client, false /* initialize rather than resume */);

  cerr << client.signature() << ": connection initialized" << endl;
}

void handle_client_info(WebSocketClient & client, const ClientInfoMsg & msg)
{
  if (not client.is_channel_initialized()) {
    return;
  }

  if (msg.init_id != client.init_id().value()) {
    cerr << client.signature() << ": warning: ignored messages with "
         << "invalid init_id (but should not have received)" << endl;
    return;
  }

  client.set_video_playback_buf(msg.video_buffer);
  client.set_audio_playback_buf(msg.audio_buffer);
  client.set_cum_rebuffer(msg.cum_rebuffer);

  /* msg.cum_rebuffer is startup delay when event is Startup */
  if (msg.event == ClientInfoMsg::Event::Startup) {
    client.set_startup_delay(msg.cum_rebuffer);
  }

  /* check if client's screen size has changed */
  if (msg.screen_width and msg.screen_height) {
    client.set_screen_size(*msg.screen_width, *msg.screen_height);

    /* record system information */
    if (enable_logging) {
      string log_line = to_string(timestamp_ms()) + "," + server_id + ","
        + expt_id + "," + client.username() + ","
        + to_string(client.first_init_id().value()) + ","
        + to_string(msg.init_id) + ","
        + client.address().ip() + ","
        + client.os() + "," + client.browser() + ","
        + to_string(*msg.screen_width) + ","
        + to_string(*msg.screen_height);
      append_to_log("client_sysinfo", log_line);
    }
  }

  /* execute the code below only if logging is enabled */
  if (enable_logging) {
    const auto channel_name = client.channel()->name();

    /* record client-info */
    string log_line = to_string(timestamp_ms()) + "," + channel_name + ","
      + server_id + "," + msg.event_str + "," + expt_id + ","
      + client.username() + ","
      + to_string(client.first_init_id().value()) + ","
      + to_string(msg.init_id) + ","
      + double_to_string(msg.video_buffer, 3) + ","
      + double_to_string(msg.cum_rebuffer, 3);
    append_to_log("client_buffer", log_line);
  }
}

void handle_client_video_ack(WebSocketClient & client,
                             const ClientVidAckMsg & msg)
{
  if (not client.is_channel_initialized()) {
    return;
  }
  auto channel = client.channel();

  if (msg.init_id != client.init_id().value()) {
    cerr << client.signature() << ": warning: ignored messages with "
         << "invalid init_id (but should not have received)" << endl;
    return;
  }

  client.set_video_playback_buf(msg.video_buffer);
  client.set_audio_playback_buf(msg.audio_buffer);
  client.set_cum_rebuffer(msg.cum_rebuffer);

  /* only interested in the event when the last segment is acked */
  if (msg.byte_offset + msg.byte_length != msg.total_byte_length) {
    return;
  }

  /* allow sending another chunk */
  client.set_client_next_vts(msg.timestamp + channel->vduration());

  /* record transmission time */
  if (client.last_video_send_ts()) {
    uint64_t trans_time = timestamp_ms() - *client.last_video_send_ts();

    /* look up media chunk size (excluding the size of init chunk size) */
    const auto data_mmap = channel->vdata(msg.video_format, msg.timestamp);
    auto media_chunk_size = get<1>(data_mmap);

    /* notify the ABR algorithm that a video chunk is acked */
    client.video_chunk_acked(msg.video_format, msg.ssim,
                             media_chunk_size, trans_time);
    client.set_last_video_send_ts(nullopt);
    client.set_tcp_info(nullopt);
  } else {
    cerr << client.signature() << ": error: server didn't send video but "
         << "received VideoAck" << endl;
    return;
  }

  /* record client's received video */
  if (enable_logging) {
    string log_line = to_string(timestamp_ms()) + "," + msg.channel + ","
      + server_id + "," + expt_id + "," + client.username() + ","
      + to_string(client.first_init_id().value()) + ","
      + to_string(msg.init_id) + ","
      + to_string(msg.timestamp) + ","
      + to_string(msg.ssim) + "," + double_to_string(msg.video_buffer, 3) + ","
      + double_to_string(msg.cum_rebuffer, 3);
    append_to_log("video_acked", log_line);
  }
}

void handle_client_audio_ack(WebSocketClient & client,
                             const ClientAudAckMsg & msg)
{
  if (not client.is_channel_initialized()) {
    return;
  }

  if (msg.init_id != client.init_id().value()) {
    cerr << client.signature() << ": warning: ignored messages with "
         << "invalid init_id (but should not have received)" << endl;
    return;
  }

  client.set_video_playback_buf(msg.video_buffer);
  client.set_audio_playback_buf(msg.audio_buffer);
  client.set_cum_rebuffer(msg.cum_rebuffer);

  /* only interested in the event when the last segment is acked */
  if (msg.byte_offset + msg.byte_length != msg.total_byte_length) {
    return;
  }

  /* allow sending another chunk */
  client.set_client_next_ats(msg.timestamp + client.channel()->aduration());
}

void create_channels(Inotify & inotify)
{
  fs::path media_dir = config["media_dir"].as<string>();

  set<string> channel_set = load_channels(config);
  for (const auto & channel_name : channel_set) {
    /* exceptions might be thrown from the lambda callbacks in the channel */
    try {
      auto channel = make_shared<Channel>(
          channel_name, media_dir,
          config["channel_configs"][channel_name], inotify);
      channels.emplace(channel_name, move(channel));
    } catch (const exception & e) {
      cerr << "Error: exceptions in channel " << channel_name << ": "
           << e.what() << endl;
    }
  }
}

bool auth_client(const string & session_key, pqxx::nontransaction & db_work)
{
  try {
    pqxx::result r = db_work.exec_prepared("auth", session_key);

    if (r.size() == 1 and r[0].size() == 1) {
      /* returned record is valid containing only true or false */
      return r[0][0].as<bool>();
    }
  } catch (const exception & e) {
    print_exception("auth_client", e);
  }

  return false;
}

void validate_id(const string & id)
{
  int id_int = -1;

  try {
    id_int = stoi(id);
  } catch (const exception &) {
    throw runtime_error("server ID and expt ID must be positive integers");
  }

  if (id_int <= 0) {
    throw runtime_error("server ID and expt ID must be positive integers");
  }
}

int run_websocket_server(pqxx::nontransaction & db_work)
{
  /* read congestion control and ABR from experimental settings */
  int server_id_int = stoi(server_id);
  int cum_servers = 0;
  YAML::Node fingerprint;

  for (const auto & node : config["experiments"]) {
    cum_servers += node["num_servers"].as<unsigned int>();
    if (server_id_int <= cum_servers) {
      fingerprint = node["fingerprint"];
      break;
    }
  }

  if (server_id_int > cum_servers) {
    throw runtime_error("Valid range for server ID is [1, " +
                        to_string(cum_servers) + "]");
  }

  string cc_name = fingerprint["cc"].as<string>();
  string abr_name = fingerprint["abr"].as<string>();
  YAML::Node abr_config;
  if (fingerprint["abr_config"]) {
    abr_config = fingerprint["abr_config"];
  }

  const string ip = "0.0.0.0";
  /* run each server on a different port */
  const uint16_t port = config["ws_base_port"].as<uint16_t>() + server_id_int;

  WebSocketServer server {{ip, port}, cc_name};

  const bool portal_debug = config["portal_settings"]["debug"].as<bool>();

  /* workaround using compiler macros (CXXFLAGS='-DNONSECURE') to create a
   * server with non-secure socket; secure socket is used by default */
  #ifdef NONSECURE
  cerr << "Launching non-secure WebSocket server on port " << port << endl;
  if (not portal_debug) {
    cerr << "Error in YAML config: 'debug' must be true in 'portal_settings'" << endl;
    return EXIT_FAILURE;
  }
  #else
  server.ssl_context().use_private_key_file(config["ssl_private_key"].as<string>());
  server.ssl_context().use_certificate_file(config["ssl_certificate"].as<string>());
  cerr << "Launching secure WebSocket server on port " << port << endl;
  if (portal_debug) {
    cerr << "Error in YAML config: 'debug' must be false in 'portal_settings'" << endl;
    return EXIT_FAILURE;
  }
  #endif

  /* create Channels and mmap existing and newly created media files */
  Inotify inotify(server.poller());
  create_channels(inotify);

  /* set server callbacks */
  server.set_message_callback(
    [&server, &db_work](const uint64_t connection_id, const WSMessage & ws_msg)
    {
      try {
        WebSocketClient & client = clients.at(connection_id);
        client.set_last_msg_recv_ts(timestamp_ms());

        ClientMsgParser msg_parser(ws_msg.payload());
        if (msg_parser.msg_type() == ClientMsgParser::Type::Init) {
          ClientInitMsg msg = msg_parser.parse_client_init();

          /* authenticate user */
          if (not client.is_authenticated()) {
            if (auth_client(msg.session_key, db_work)) {
              client.set_authenticated(true);

              /* set client's username and IP */
              client.set_session_key(msg.session_key);
              client.set_username(msg.username);
              client.set_address(server.peer_addr(connection_id));

              /* set client's system info (OS, browser and screen size) */
              client.set_os(msg.os);
              client.set_browser(msg.browser);
              client.set_screen_size(msg.screen_width, msg.screen_height);

              cerr << connection_id << ": authentication succeeded" << endl;
              cerr << client.signature() << ": " << client.browser() << " on "
                   << client.os() << ", " << client.address().str() << endl;
            } else {
              cerr << connection_id << ": authentication failed" << endl;
              server.close_connection(connection_id);
              return;
            }
          }

          /* handle client-init and initialize client's channel */
          handle_client_init(server, client, msg);
        } else {
          /* parse a message other than client-init only if user is authed */
          if (not client.is_authenticated()) {
            cerr << connection_id << ": ignored messages from a "
                 << "non-authenticated user" << endl;
            server.close_connection(connection_id);
            return;
          }

          switch (msg_parser.msg_type()) {
          case ClientMsgParser::Type::Info:
            handle_client_info(client, msg_parser.parse_client_info());
            break;
          case ClientMsgParser::Type::VideoAck:
            handle_client_video_ack(client, msg_parser.parse_client_vidack());
            break;
          case ClientMsgParser::Type::AudioAck:
            handle_client_audio_ack(client, msg_parser.parse_client_audack());
            break;
          default:
            throw runtime_error("invalid client message");
          }
        }

        /* try serving media to this client */
        serve_client(server, client);
      } catch (const exception & e) {
        cerr << client_signature(connection_id)
             << ": warning in message callback: " << e.what() << endl;
        server.close_connection(connection_id);
      }
    }
  );

  server.set_open_callback(
    [&server, &abr_name, &abr_config](const uint64_t connection_id)
    {
      try {
        cerr << connection_id << ": connection opened" << endl;

        /* check if number of connections already exceeds the limit */
        if (clients.size() >= MAX_CONNECTION_NUM) {
          cerr << connection_id << ": rejected over-limit connection" << endl;

          WebSocketClient tmp_client(connection_id, abr_name, abr_config);
          send_server_error(server, tmp_client, ServerErrorMsg::Type::Limit);
          server.close_connection(connection_id);
          return;
        }

        /* create a new WebSocketClient */
        clients.emplace(
            piecewise_construct,
            forward_as_tuple(connection_id),
            forward_as_tuple(connection_id, abr_name, abr_config));
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
        clients.erase(connection_id);
        cerr << connection_id << ": connection closed" << endl;
      } catch (const exception & e) {
        cerr << client_signature(connection_id)
             << ": warning in close callback: " << e.what() << endl;
      }
    }
  );

  /* start a slow timer to perform some tasks */
  Timerfd slow_timer;
  start_slow_timer(slow_timer, server);

  slow_timer.start(1000, 1000);  /* slow timer fires every second */

  return server.loop();
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

  /* load YAML settings */
  config = YAML::LoadFile(argv[1]);
  enable_logging = config["enable_logging"].as<bool>();
  if (enable_logging) {
    cerr << "Logging is enabled" << endl;
    log_dir = config["log_dir"].as<string>();
  } else {
    cerr << "Logging is disabled" << endl;
  }

  server_id = argv[2];
  validate_id(server_id);

  if (enable_logging) {
    if (argc != 4) {
      cerr << "expt ID must be provided if enable_logging is true" << endl;
      return EXIT_FAILURE;
    }

    expt_id = argv[3];
    validate_id(expt_id);
  }

  /* ignore SIGPIPE generated by SSL_write */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    throw runtime_error("signal: failed to ignore SIGPIPE");
  }

  /* connect to the database for user authentication */
  string db_conn_str = postgres_connection_string(config["postgres_connection"]);
  pqxx::connection db_conn(db_conn_str);
  cerr << "Connected to PostgreSQL at " << db_conn.hostname() << endl;

  /* prepare a statement to check if the session_key in client-init is valid */
  db_conn.prepare("auth", "SELECT EXISTS(SELECT 1 FROM django_session WHERE "
    "session_key = $1 AND expire_date > now());");

  /* reuse the same nontransaction as the server only reads the database */
  pqxx::nontransaction db_work(db_conn);

  /* run a WebSocketServer instance */
  return run_websocket_server(db_work);
}
