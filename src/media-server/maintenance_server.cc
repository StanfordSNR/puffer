#include <iostream>
#include <string>

#include "yaml-cpp/yaml.h"
#include "ws_server.hh"
#include "server_message.hh"

using namespace std;

#ifdef NONSECURE
using WebSocketServer = WebSocketTCPServer;
#else
using WebSocketServer = WebSocketSecureServer;
#endif

void print_usage(const string & program_name)
{
  cerr << program_name << " <YAML configuration> <server ID>" << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* load YAML settings */
  YAML::Node config = YAML::LoadFile(argv[1]);
  int server_id_int = stoi(argv[2]);

  const string ip = "0.0.0.0";
  const uint16_t port = config["ws_base_port"].as<uint16_t>() + server_id_int;
  WebSocketServer server {{ip, port}, "cubic"};

  const bool portal_debug = config["portal_settings"]["debug"].as<bool>();
  #ifdef NONSECURE
  cerr << "Launching non-secure maintenance server" << endl;
  if (not portal_debug) {
    cerr << "Error in YAML config: 'debug' must be true in 'portal_settings'" << endl;
    return EXIT_FAILURE;
  }
  #else
  server.ssl_context().use_private_key_file(config["ssl_private_key"].as<string>());
  server.ssl_context().use_certificate_file(config["ssl_certificate"].as<string>());
  cerr << "Launching secure maintenance server" << endl;
  if (portal_debug) {
    cerr << "Error in YAML config: 'debug' must be false in 'portal_settings'" << endl;
    return EXIT_FAILURE;
  }
  #endif

  server.set_open_callback(
    [&server](const uint64_t connection_id)
    {
      cerr << connection_id << ": connection opened" << endl;

      /* maintenance error message can have an arbitrary init_id */
      ServerErrorMsg err_msg(0, ServerErrorMsg::Type::Maintenance);
      WSFrame frame {true, WSFrame::OpCode::Binary, err_msg.to_string()};
      server.queue_frame(connection_id, frame);
      server.close_connection(connection_id);
    }
  );

  server.set_close_callback(
    [](const uint64_t connection_id)
    {
      cerr << connection_id << ": connection closed" << endl;
    }
  );

  return server.loop();
}
