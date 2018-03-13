#include "ws_client.hh"

// TODO: fix this constructor
WebSocketClient::WebSocketClient(const uint64_t connection_id)
  : connection_id_(connection_id)
{}

WebSocketClient::initialize(const string & channel, const uint64_t vts,
                            const uint64_t ats)
{}