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

static bool debug = false;

/* global settings */
static const size_t MAX_WS_FRAME_B = 100 * 1024;  /* 10 KB */

/* drop a client if have not received messaged from it for 10 seconds */
static const unsigned int MAX_IDLE_S = 10;

static map<string, shared_ptr<Channel>> channels;  /* key: channel name */
static map<uint64_t, WebSocketClient> clients;  /* key: connection ID */

static Timerfd global_timer;  /* non-blocking global timer fd for scheduling */

/* for logging */
static string server_id;
static fs::path log_dir;  /* parent directory for logging */
static map<string, FileDescriptor> log_fds;  /* map log name to fd */
static const unsigned int MAX_LOG_FILESIZE = 10 * 1024 * 1024;  /* 10 MB */
static unsigned int log_rebuffer_num = 0;
static time_t last_minute = 0;

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
    if (client_pair.second.is_rebuffering()) {
      log_rebuffer_num++;
    }
  }

  /* update corresponding logs */
  append_log_rebuffer_rate(time(nullptr));
}

void serve_video_to_client(WebSocketServer & server, WebSocketClient & client)
{
  const auto & channel = client.channel();

  uint64_t next_vts = client.next_vts().value();
  /* new chunk is not ready yet */
  if (not channel->vready(next_vts)) {
    return;
  }

  /* select a video quality using ABR algorithm */
  const VideoFormat & next_vq = client.select_video_format();
  double ssim = channel->vssim(next_vts).at(next_vq);

  /* check if a new init segment is needed */
  optional<mmap_t> init_mmap;
  if (not client.curr_vq() or next_vq != client.curr_vq().value()) {
    init_mmap = channel->vinit(next_vq);
  }

  /* construct the next segment to send */
  const auto & data_mmap = channel->vdata(next_vq, next_vts);
  VideoSegment next_vsegment {next_vq, data_mmap, init_mmap};

  /* divide the next segment into WebSocket frames and send */
  while (not next_vsegment.done()) {
    ServerVideoMsg video_msg(channel->name(),
                             next_vsegment.format().to_string(),
                             ssim,
                             next_vts,
                             channel->vduration(),
                             next_vsegment.offset(),
                             next_vsegment.length());
    string frame_payload = video_msg.to_string();
    next_vsegment.read(frame_payload, MAX_WS_FRAME_B - frame_payload.size());

    WSFrame frame {true, WSFrame::OpCode::Binary, move(frame_payload)};
    server.queue_frame(client.connection_id(), frame);
  }

  /* finish sending */
  client.set_next_vts(next_vts + channel->vduration());
  client.set_curr_vq(next_vsegment.format());
  client.set_last_video_send_ts(timestamp_ms());

  cerr << client.signature() << ": channel " << channel->name()
       << ", video " << next_vts << " " << next_vq << " " << ssim << endl;
}

void serve_audio_to_client(WebSocketServer & server, WebSocketClient & client)
{
  const auto & channel = client.channel();

  uint64_t next_ats = client.next_ats().value();
  /* new chunk is not ready yet */
  if (not channel->aready(next_ats)) {
    return;
  }

  /* select an audio quality using ABR algorithm */
  const AudioFormat & next_aq = client.select_audio_format();

  /* check if a new init segment is needed */
  optional<mmap_t> init_mmap;
  if (not client.curr_aq() or next_aq != client.curr_aq().value()) {
    init_mmap = channel->ainit(next_aq);
  }

  /* construct the next segment to send */
  const auto & data_mmap = channel->adata(next_aq, next_ats);
  AudioSegment next_asegment {next_aq, data_mmap, init_mmap};

  /* divide the next segment into WebSocket frames and send */
  while (not next_asegment.done()) {
    ServerAudioMsg audio_msg(channel->name(),
                             next_asegment.format().to_string(),
                             next_ats,
                             channel->aduration(),
                             next_asegment.offset(),
                             next_asegment.length());
    string frame_payload = audio_msg.to_string();
    next_asegment.read(frame_payload, MAX_WS_FRAME_B - frame_payload.size());

    WSFrame frame {true, WSFrame::OpCode::Binary, move(frame_payload)};
    server.queue_frame(client.connection_id(), frame);
  }

  /* finish sending */
  client.set_next_ats(next_ats + channel->aduration());
  client.set_curr_aq(next_asegment.format());

  cerr << client.signature() << ": channel " << channel->name()
       << ", audio " << next_ats << " " << next_aq << endl;
}

void reinit_laggy_client(WebSocketServer & server, WebSocketClient & client,
                         const shared_ptr<Channel> & channel)
{
  uint64_t init_vts = channel->init_vts().value();
  uint64_t init_ats = channel->init_ats().value();

  cerr << client.signature() << ": reinitialize laggy client "
       << client.next_vts().value() << "->" << init_vts << endl;
  client.init(channel, init_vts, init_ats);

  ServerInitMsg reinit(channel->name(), channel->vcodec(),
                       channel->acodec(), channel->timescale(),
                       client.next_vts().value(),
                       client.next_ats().value(),
                       client.init_id(), false);
  WSFrame frame {true, WSFrame::OpCode::Binary, reinit.to_string()};
  server.queue_frame(client.connection_id(), frame);
}

void serve_client(WebSocketServer & server, WebSocketClient & client)
{
  const auto & channel = client.channel();

  if (not channel->ready()) {
    cerr << client.signature()
         << ": cannot serve because channel is not available anymore" << endl;
    return;
  }

  if (channel->live()) {
    /* reinit client if clean frontiers have caught up */
    if ((channel->vclean_frontier() and
         client.next_vts().value() <= *channel->vclean_frontier()) or
        (channel->aclean_frontier() and
         client.next_ats().value() <= *channel->aclean_frontier())) {
      reinit_laggy_client(server, client, channel);
      return;
    }
  }

  /* wait for VideoAck and AudioAck before sending the next chunk */
  const bool can_send_video =
      client.video_playback_buf() <= WebSocketClient::MAX_BUFFER_S and
      client.video_in_flight().value() == 0;
  const bool can_send_audio =
      client.audio_playback_buf() <= WebSocketClient::MAX_BUFFER_S and
      client.audio_in_flight().value() == 0;

  if (client.next_vts().value() > client.next_ats().value()) {
    /* prioritize audio */
    if (can_send_audio) {
      serve_audio_to_client(server, client);
    }
    if (can_send_video) {
      serve_video_to_client(server, client);
    }
  } else {
    /* prioritize video */
    if (can_send_video) {
      serve_video_to_client(server, client);
    }
    if (can_send_audio) {
      serve_audio_to_client(server, client);
    }
  }
}

void log_active_streams(const time_t this_minute)
{
  /* channel name -> count */
  map<string, unsigned int> active_streams_count;

  for (const auto & client_pair : clients) {
    const auto & client = client_pair.second;
    if (client.channel()) {
      string channel_name = client.channel()->name();

      auto map_it = active_streams_count.find(channel_name);
      if (map_it == active_streams_count.end()) {
        active_streams_count.emplace(channel_name, 1);
      } else {
        map_it->second += 1;
      }
    }
  }

  for (const auto & channel_count : active_streams_count) {
    string log_line = to_string(this_minute) + " " + channel_count.first
                      + " " + to_string(channel_count.second);
    append_to_log("active_streams", log_line);
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

      /* perform some tasks once per minute */
      time_t this_minute = curr_time - curr_time % 60;
      if (this_minute > last_minute) {
        last_minute = this_minute;

        /* write active_streams count to file */
        log_active_streams(this_minute);
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
  const auto & channel = client.channel();

  ServerInitMsg init(channel->name(), channel->vcodec(),
                     channel->acodec(), channel->timescale(),
                     client.next_vts().value(),
                     client.next_ats().value(),
                     client.init_id(), can_resume);
  WSFrame frame {true, WSFrame::OpCode::Binary, init.to_string()};

  /* drop previously queued frames before sending server init */
  server.clear_buffer(client.connection_id());
  server.queue_frame(client.connection_id(), frame);
}

bool resume_connection(WebSocketServer & server, WebSocketClient & client,
                       const ClientInitMsg & msg,
                       const shared_ptr<Channel> & channel)
{
  /* don't resume a connection if client is requesting a different channel */
  if (client.channel() != channel) {
    return false;
  }

  /* check if the requested timestamps are valid */
  if (not (msg.next_vts and channel->is_valid_vts(msg.next_vts.value()) and
           msg.next_ats and channel->is_valid_ats(msg.next_ats.value()))) {
    return false;
  }

  uint64_t requested_vts = msg.next_vts.value();
  uint64_t requested_ats = msg.next_ats.value();

  if (channel->live()) {
    /* don't resume if the requested video is already behind the live edge */
    if (not channel->live_edge() or
        requested_vts < channel->live_edge().value()) {
      return false;
    }

    /* don't resume if the requested chunks are not ready */
    if (not channel->vready_frontier() or
        requested_vts > *channel->vready_frontier() or
        not channel->aready_frontier() or
        requested_ats > *channel->aready_frontier()) {
      return false;
    }
  } else {
    /* don't resume if the requested chunks are not ready */
    if (not (channel->vready(requested_vts) and
             channel->aready(requested_ats))) {
      return false;
    }
  }

  /* reinitialize the client */
  client.init(channel, requested_vts, requested_ats);
  send_server_init(server, client, true /* can resume */);

  cerr << client.signature() << ": connection resumed" << endl;
  return true;
}

void update_screen_size(WebSocketClient & client,
                        const uint16_t new_screen_height,
                        const uint16_t new_screen_width)
{
  if (client.screen_height() == new_screen_height and
      client.screen_width() == new_screen_width) {
    return;
  }

  client.set_screen_height(new_screen_height);
  client.set_screen_width(new_screen_width);

  if (client.channel()) {
    client.set_max_video_size(client.channel()->vformats());
  }
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

  const auto & channel = it->second;

  /* ignore client-init if the channel is not ready */
  if (not channel->ready()) {
    cerr << client.signature()
         << ": ignored client-init (channel is not ready)" << endl;
    return;
  }

  /* check if the streaming can be resumed */
  if (resume_connection(server, client, msg, channel)) {
    return;
  }

  uint64_t init_vts = channel->init_vts().value();
  uint64_t init_ats = channel->init_ats().value();

  client.init(channel, init_vts, init_ats);

  /* update client's screen size after setting client's channel */
  update_screen_size(client, msg.screen_height, msg.screen_width);

  /* reinit the log data */
  reinit_log_data();

  send_server_init(server, client, false /* initialize rather than resume */);
  cerr << client.signature() << ": connection initialized" << endl;
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

  /* check if client's screen size has changed */
  update_screen_size(client, msg.screen_height, msg.screen_width);

  uint64_t cur_time = time(nullptr);

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
        + client.channel()->name() + " "
        + to_string(static_cast<int>(msg.event)) + " " + buf;
    append_to_log("playback_buffer", log_line);
  } else if (msg.event == ClientInfoMsg::PlayerEvent::VideoAck) {
    /* VideoAck for the last video segment */
    if (*msg.byte_offset + *msg.received_bytes == *msg.total_byte_length) {
      /* record transmission time */
      if (client.last_video_send_ts()) {
        uint64_t trans_time = timestamp_ms() - *client.last_video_send_ts();
        /* notify the ABR algorithm that a video chunk is acked */
        client.video_chunk_acked(client.curr_vq().value(), *msg.ssim,
                                 *msg.total_byte_length, trans_time);
        client.reset_last_video_send_ts();
      } else {
        cerr << client.signature() << ": error: server didn't send video but "
             << "received VideoAck" << endl;
        return;
      }

      /* record video quality */
      string log_line = to_string(cur_time) + " " + client.username() + " "
          + client.channel()->name() + " " + to_string(*msg.timestamp) + " "
          + *msg.quality + " " + to_string(*msg.ssim);
      append_to_log("video_quality", log_line);
    }

    /* get current throughput (measured in kpbs) of the client */
    if (*msg.receiving_time_ms > 0 and *msg.receiving_time_ms < 3000) {
      client.set_curr_tput(*msg.received_bytes * 8.0 / *msg.receiving_time_ms);
    }
  }

  /* record rebuffer event and rebuffer rate */
  if (msg.event == ClientInfoMsg::PlayerEvent::Rebuffer or
      msg.event == ClientInfoMsg::PlayerEvent::CanPlay) {
    if (msg.event == ClientInfoMsg::PlayerEvent::Rebuffer) {
      string log_line = to_string(cur_time) + " " + client.username() + " "
                        + client.channel()->name();
      append_to_log("rebuffer_event", log_line);
    }

    if (not client.is_rebuffering() and
        msg.event == ClientInfoMsg::PlayerEvent::Rebuffer) {
      log_rebuffer_num++;
      client.set_rebuffering(true);
      append_log_rebuffer_rate(cur_time);
    }
    if (client.is_rebuffering() and
        msg.event == ClientInfoMsg::PlayerEvent::CanPlay) {
      log_rebuffer_num--;
      client.set_rebuffering(false);
      append_log_rebuffer_rate(cur_time);
    }
  }
}

void load_channels(const YAML::Node & config, Inotify & inotify)
{
  /* load channels */
  for (YAML::const_iterator it = config["channels"].begin();
       it != config["channels"].end(); ++it) {
    const string & channel_name = it->as<string>();

    if (not config["channel_configs"][channel_name]) {
      throw runtime_error("Cannot find details of channel: " + channel_name);
    }

    if (channels.find(channel_name) != channels.end()) {
      throw runtime_error("Found duplicate channel: " + channel_name);
    }

    /* exceptions might be thrown from the lambda callbacks in the channel */
    try {
      auto channel = make_shared<Channel>(
          channel_name, config["channel_configs"][channel_name], inotify);
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

  /* obtain and validate server ID */
  server_id = argv[2];
  try {
    stoi(server_id);
  } catch (const exception &) {
    cerr << "invalid server ID: " << server_id << endl;
    return EXIT_FAILURE;
  }

  /* load YAML settings */
  YAML::Node config = YAML::LoadFile(argv[1]);
  log_dir = config["log_dir"].as<string>();

  /* ignore SIGPIPE generated by SSL_write */
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    cerr << "signal: failed to ignore SIGPIPE" << endl;
    return EXIT_FAILURE;
  }

  /* create a WebSocketServer instance */
  const string ip = "0.0.0.0";
  const uint16_t port = config["port"].as<uint16_t>();
  string congestion_control = "default";
  if (config["congestion_control"]) {
    congestion_control = config["congestion_control"].as<string>();
  }
  WebSocketServer server {{ip, port}, congestion_control};

  /* workaround using compiler macros (CXXFLAGS='-DNONSECURE') to create a
   * server with non-secure socket; secure socket is used by default */
  #ifdef NONSECURE
  cerr << "Launching non-secure WebSocket server" << endl;
  #else
  server.ssl_context().use_private_key_file(config["private_key"].as<string>());
  server.ssl_context().use_certificate_file(config["certificate"].as<string>());
  cerr << "Launching secure WebSocket server" << endl;
  #endif

  /* connect to the database for user authentication */
  string db_conn_str = config["db_connection"].as<string>();
  db_conn_str += " password=" + safe_getenv("PUFFER_PORTAL_DB_KEY");

  pqxx::connection db_conn(db_conn_str);
  cerr << "Connected to database: " << db_conn.hostname() << endl;

  /* reuse the same nontransaction as the server reads the database only */
  pqxx::nontransaction db_work(db_conn);

  /* prepare a statement to check if the session_key in client-init is valid */
  db_conn.prepare("auth", "SELECT EXISTS(SELECT 1 FROM django_session WHERE "
    "session_key = $1 AND expire_date > now());");

  /* load channels and mmap existing and newly created media files */
  Inotify inotify(server.poller());
  load_channels(config, inotify);

  /* load ABR algorithm */
  string abr_name = "linear_bba";  /* default ABR */
  if (config["abr_algorithm"]) {
    abr_name = config["abr_algorithm"].as<string>();
  }
  const YAML::Node & abr_config = config["abr_configs"][abr_name];

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

          /* authenticate user */
          if (not client.is_authenticated()) {
            if (auth_client(init_msg.session_key, db_work)) {
              client.set_authenticated(true);

              /* set client's username and IP */
              client.set_session_key(init_msg.session_key);
              client.set_username(init_msg.username);
              client.set_address(server.peer_addr(connection_id));

              /* set client's browser and system info */
              client.set_browser(init_msg.browser);
              client.set_os(init_msg.os);

              cerr << connection_id << ": authentication succeeded" << endl;
              cerr << client.signature() << ": " << client.browser() << " on "
                   << client.os() << ", " << client.address().str() << endl;
            } else {
              cerr << connection_id << ": authentication failed" << endl;
              server.close_connection(connection_id);
              return;
            }
          }

          handle_client_init(server, client, init_msg);
        } else {
          /* parse a message other than client-init only if user is authed */
          if (not client.is_authenticated()) {
            cerr << connection_id << ": ignored messages from a "
                 << "non-authenticated user" << endl;
            server.close_connection(connection_id);
            return;
          }

          if (parser.msg_type() == ClientMsgParser::Type::Info) {
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
    [&server, &abr_name, &abr_config](const uint64_t connection_id)
    {
      try {
        cerr << connection_id << ": connection opened" << endl;

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
        cerr << connection_id << ": connection closed" << endl;

        clients.erase(connection_id);

        /* TODO: the logging of rebuffer rate still has flaws */
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
