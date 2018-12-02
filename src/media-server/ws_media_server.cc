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
#include "yaml.hh"
#include "util.hh"
#include "strict_conversions.hh"
#include "timestamp.hh"
#include "media_formats.hh"
#include "inotify.hh"
#include "timerfd.hh"
#include "channel.hh"
#include "server_message.hh"
#include "client_message.hh"
#include "ws_server.hh"
#include "ws_client.hh"
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
static const unsigned int DROP_NOTIFICATION_MS = 30000;
static const unsigned int MAX_IDLE_MS = 60000;

/* for logging */
static bool enable_logging = false;
static fs::path log_dir;  /* base directory for logging */
static string server_id;
static string expt_id;
static string group_id;
static map<string, FileDescriptor> log_fds;  /* map log name to fd */
static const unsigned int MAX_LOG_FILESIZE = 100 * 1024 * 1024;  /* 100 MB */
static uint64_t last_minute = 0;  /* in ms; multiple of 60000 */

void print_usage(const string & program_name)
{
  cerr <<
  program_name << " <YAML configuration> [<server ID> <expt ID> <group ID>]"
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

void serve_video_to_client(WebSocketServer & server, WebSocketClient & client)
{
  if (not client.next_vts()) { return; }
  uint64_t next_vts = *client.next_vts();

  const auto channel = client.channel();
  if (not channel->vready_to_serve(next_vts)) {
    return;
  }

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
    ServerVideoMsg video_msg(client.init_id(),
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
}

void serve_audio_to_client(WebSocketServer & server, WebSocketClient & client)
{
  if (not client.next_ats()) { return; }
  uint64_t next_ats = *client.next_ats();

  const auto channel = client.channel();
  if (not channel->aready_to_serve(next_ats)) {
    return;
  }

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
    ServerAudioMsg audio_msg(client.init_id(),
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
  /* client should already have valid next_vts and next_ats */
  if (not client.next_vts() or not client.next_ats()) {
    return;
  }

  const auto channel = client.channel();

  ServerInitMsg init(client.init_id(), channel->name(),
                     channel->vcodec(), channel->acodec(),
                     channel->timescale(),
                     channel->vduration(), channel->aduration(),
                     *client.next_vts(), *client.next_ats(),
                     can_resume);
  WSFrame frame {true, WSFrame::OpCode::Binary, init.to_string()};

  /* drop previously queued frames before sending server-init */
  server.clear_buffer(client.connection_id());

  server.queue_frame(client.connection_id(), frame);
}

void send_server_error(WebSocketServer & server, WebSocketClient & client,
                       const ServerErrorMsg::ErrorType error_type)
{
  ServerErrorMsg err_msg(client.init_id(), error_type);
  WSFrame frame {true, WSFrame::OpCode::Binary, err_msg.to_string()};

  /* drop previously queued frames before sending server-error */
  server.clear_buffer(client.connection_id());

  server.queue_frame(client.connection_id(), frame);
}

void reinit_laggy_client(WebSocketServer & server, WebSocketClient & client,
                         const shared_ptr<Channel> & channel)
{
  uint64_t init_vts = channel->init_vts().value();
  uint64_t init_ats = channel->init_ats().value();

  client.init_channel(channel, init_vts, init_ats);
  send_server_init(server, client, false /* cannot resume */);

  cerr << client.signature() << ": reinitialize laggy client" << endl;
}

void serve_client(WebSocketServer & server, WebSocketClient & client)
{
  const auto channel = client.channel();

  /* channel may become not ready */
  if (not channel or not channel->ready_to_serve()) {
    return;
  }

  if (channel->live()) {
    /* reinit client if clean frontiers have caught up */
    if ((channel->vclean_frontier() and client.next_vts() and
         *client.next_vts() <= *channel->vclean_frontier()) or
        (channel->aclean_frontier() and client.next_ats() and
         *client.next_ats() <= *channel->aclean_frontier())) {
      reinit_laggy_client(server, client, channel);
      return;
    }
  }

  /* wait for VideoAck and AudioAck before sending the next chunk */
  if (client.video_playback_buf() <= WebSocketClient::MAX_BUFFER_S and
      client.video_in_flight() and *client.video_in_flight() == 0) {
    serve_video_to_client(server, client);
  }

  if (client.audio_playback_buf() <= WebSocketClient::MAX_BUFFER_S and
      client.audio_in_flight() and *client.audio_in_flight() == 0) {
    serve_audio_to_client(server, client);
  }
}

void log_active_streams(const uint64_t this_minute)
{
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
    string log_line = to_string(this_minute) + " " + channel_name + " "
      + expt_id + " " + group_id + " " +  server_id + " " + to_string(count);
    append_to_log("active_streams", log_line);
  }
}

void start_fast_timer(Timerfd & fast_timer, WebSocketServer & server)
{
  server.poller().add_action(Poller::Action(fast_timer, Direction::In,
    [&fast_timer, &server]()->Result {
      /* must read the timerfd, and check if timer has fired */
      if (fast_timer.expirations() == 0) {
        return ResultType::Continue;
      }

      /* iterate over all connections */
      for (auto & client_pair : clients) {
        WebSocketClient & client = client_pair.second;
        serve_client(server, client);
      }

      return ResultType::Continue;
    }
  ));
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
        const auto last_msg_recv_ts = client.last_msg_recv_ts();
        if (last_msg_recv_ts) {
          const auto elapsed = timestamp_ms() - *last_msg_recv_ts;

          assert(MAX_IDLE_MS > DROP_NOTIFICATION_MS);
          if (elapsed > MAX_IDLE_MS) {
            connections_to_clean.emplace(connection_id);
            cerr << client.signature() << ": cleaned idle connection" << endl;
            continue;
          } else if (elapsed > DROP_NOTIFICATION_MS) {
            /* notify that the connection is going to be dropped */
            send_server_error(server, client, ServerErrorMsg::ErrorType::Drop);
            cerr << client.signature() << ": notified client to drop" << endl;
            continue;
          }
        }
      }

      /* connections can be safely cleaned now */
      for (const uint64_t connection_id : connections_to_clean) {
        clients.erase(connection_id);
        server.clean_idle_connection(connection_id);
      }

      if (enable_logging) {
        /* perform some tasks once per minute */
        const auto curr_time = timestamp_ms();
        const auto this_minute = curr_time - curr_time % 60000;

        if (this_minute > last_minute) {
          last_minute = this_minute;

          /* write active_streams count to file */
          log_active_streams(this_minute);
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
  client.set_last_msg_recv_ts(timestamp_ms());

  /* always set client's init_id when a client-init is received */
  client.set_init_id(msg.init_id);

  /* ignore invalid channel request */
  auto it = channels.find(msg.channel);
  if (it == channels.end()) {
    cerr << client.signature() << ": requested channel "
         << msg.channel << " not found" << endl;
    return;
  }

  const auto & channel = it->second;

  /* reply that the channel is not ready */
  if (not channel->ready_to_serve()) {
    send_server_error(server, client, ServerErrorMsg::ErrorType::Channel);
    cerr << client.signature() << ": requested channel " << channel->name()
         << " is not ready" << endl;
    return;
  }

  /* record client-init */
  if (enable_logging) {
    string log_line = to_string(timestamp_ms()) + " " + msg.channel + " "
      + expt_id + " " + group_id + " " + client.username() + " "
      + to_string(msg.init_id) + " init 0 0" /* event buffer cum_rebuf */;
    append_to_log("client_buffer", log_line);
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
  if (msg.init_id != client.init_id()) {
    cerr << client.signature() << ": warning: ignored messages with "
         << "invalid init_id (but should not have received)" << endl;
    return;
  }

  /* server does not count client-info timer as last_msg_recv_ts */
  if (msg.event != ClientInfoMsg::Event::Timer) {
    client.set_last_msg_recv_ts(timestamp_ms());
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
  }

  /* execute the code below only if logging is enabled */
  if (enable_logging) {
    const auto channel_name = client.channel()->name();

    /* record client-info */
    string log_line = to_string(timestamp_ms()) + " " + channel_name + " "
      + expt_id + " " + group_id + " " + client.username() + " "
      + to_string(msg.init_id) + " " + msg.event_str + " "
      + double_to_string(msg.video_buffer, 3) + " "
      + double_to_string(msg.cum_rebuffer, 3);
    append_to_log("client_buffer", log_line);

    /* record rebuffer events */
    if (msg.event == ClientInfoMsg::Event::Rebuffer) {
      string log_line = to_string(timestamp_ms()) + " " + channel_name + " "
        + expt_id + " " + group_id + " " + server_id;
      append_to_log("rebuffer_events", log_line);
    }
  }
}

void handle_client_video_ack(WebSocketClient & client,
                             const ClientVidAckMsg & msg)
{
  if (msg.init_id != client.init_id()) {
    cerr << client.signature() << ": warning: ignored messages with "
         << "invalid init_id (but should not have received)" << endl;
    return;
  }

  client.set_last_msg_recv_ts(timestamp_ms());

  client.set_video_playback_buf(msg.video_buffer);
  client.set_audio_playback_buf(msg.audio_buffer);
  client.set_cum_rebuffer(msg.cum_rebuffer);

  /* only interested in the event when the last segment is acked */
  if (msg.byte_offset + msg.byte_length != msg.total_byte_length) {
    return;
  }

  /* allow sending another chunk */
  client.set_client_next_vts(msg.timestamp + client.channel()->vduration());

  /* record transmission time */
  if (client.last_video_send_ts()) {
    uint64_t trans_time = timestamp_ms() - *client.last_video_send_ts();

    /* notify the ABR algorithm that a video chunk is acked */
    client.video_chunk_acked(msg.video_format, msg.ssim,
                             msg.total_byte_length, trans_time);
    client.set_last_video_send_ts(nullopt);
  } else {
    cerr << client.signature() << ": error: server didn't send video but "
         << "received VideoAck" << endl;
    return;
  }

  /* record client's received video */
  if (enable_logging) {
    string log_line = to_string(timestamp_ms()) + " " + msg.channel + " "
      + expt_id + " " + group_id + " " + client.username() + " "
      + to_string(msg.init_id) + " " + msg.format + " "
      + double_to_string(msg.ssim, 3);
    append_to_log("client_video", log_line);
  }
}

void handle_client_audio_ack(WebSocketClient & client,
                             const ClientAudAckMsg & msg)
{
  if (msg.init_id != client.init_id()) {
    cerr << client.signature() << ": warning: ignored messages with "
         << "invalid init_id (but should not have received)" << endl;
    return;
  }

  client.set_last_msg_recv_ts(timestamp_ms());

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
    pqxx::result r = db_work.prepared("auth")(session_key).exec();

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
    throw runtime_error("server ID, expt ID, and group ID must be positive integers");
  }

  if (id_int <= 0) {
    throw runtime_error("server ID, expt ID, and group ID must be positive integers");
  }
}

int run_websocket_server(pqxx::nontransaction & db_work)
{
  /* default congestion control and ABR algorithm */
  string congestion_control = "default";
  string abr_name = "linear_bba";

  /* read congestion control and ABR from experimental settings */
  if (not group_id.empty()) {
    const auto & expt_config = config["experimental_groups"][group_id];
    congestion_control = expt_config["congestion_control"].as<string>();
    abr_name = expt_config["abr_algorithm"].as<string>();
  }

  const string ip = "0.0.0.0";
  const uint16_t port = config["ws_port"].as<uint16_t>();
  WebSocketServer server {{ip, port}, congestion_control};

  const bool portal_debug = config["portal_settings"]["debug"].as<bool>();
  /* workaround using compiler macros (CXXFLAGS='-DNONSECURE') to create a
   * server with non-secure socket; secure socket is used by default */
  #ifdef NONSECURE
  cerr << "Launching non-secure WebSocket server" << endl;
  if (not portal_debug) {
    cerr << "Error in YAML config: 'debug' must be true in 'portal_settings'" << endl;
    return EXIT_FAILURE;
  }
  #else
  server.ssl_context().use_private_key_file(config["ssl_private_key"].as<string>());
  server.ssl_context().use_certificate_file(config["ssl_certificate"].as<string>());
  cerr << "Launching secure WebSocket server" << endl;
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

          const auto channel = client.channel();
          if (not channel or not channel->ready_to_serve()) {
            /* notify the client that the requested channel is not available */
            send_server_error(server, client,
                              ServerErrorMsg::ErrorType::Channel);
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
      } catch (const exception & e) {
        cerr << client_signature(connection_id)
             << ": warning in message callback: " << e.what() << endl;
        server.close_connection(connection_id);
      }
    }
  );

  server.set_open_callback(
    [&server, abr_name](const uint64_t connection_id)
    {
      try {
        cerr << connection_id << ": connection opened" << endl;

        /* create a new WebSocketClient */
        clients.emplace(
            piecewise_construct,
            forward_as_tuple(connection_id),
            forward_as_tuple(connection_id, abr_name,
                             config["abr_configs"][abr_name]));
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

  /* start a fast timer that fires once per 50ms to serve media to clients */
  Timerfd fast_timer;
  start_fast_timer(fast_timer, server);

  /* start a slow timer to perform some other tasks */
  Timerfd slow_timer;
  start_slow_timer(slow_timer, server);

  fast_timer.start(100, 100);    /* fast timer fires every 100 ms */
  slow_timer.start(1000, 1000);  /* slow timer fires every second */

  return server.loop();
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2 and argc != 5) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* load YAML settings */
  config = YAML::LoadFile(argv[1]);
  enable_logging = config["enable_logging"].as<bool>();

  if (argc == 2 and enable_logging) {
    cerr << "Must specify server ID, expt ID, and group ID "
         << "if enable_logging is true" << endl;
    return EXIT_FAILURE;
  }

  if (argc == 5 and enable_logging) {
    log_dir = config["log_dir"].as<string>();
    server_id = argv[2];
    validate_id(server_id);
    expt_id = argv[3];
    validate_id(expt_id);
    group_id = argv[4];
    validate_id(group_id);
  }

  /* ignore SIGPIPE generated by SSL_write */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    throw runtime_error("signal: failed to ignore SIGPIPE");
  }

  /* connect to the database for user authentication */
  string db_conn_str = postgres_connection_string(config["postgres_connection"]);
  pqxx::connection db_conn(db_conn_str);
  cerr << "Connected to database: " << db_conn.hostname() << endl;

  /* prepare a statement to check if the session_key in client-init is valid */
  db_conn.prepare("auth", "SELECT EXISTS(SELECT 1 FROM django_session WHERE "
    "session_key = $1 AND expire_date > now());");

  /* reuse the same nontransaction as the server only reads the database */
  pqxx::nontransaction db_work(db_conn);

  /* run a WebSocketServer instance */
  return run_websocket_server(db_work);
}
