#include "pensieve.hh"
#include "ws_client.hh"
#include "pid.hh"
#include "system_runner.hh"
#include "serialization.hh"
#include "ipc_socket.hh"
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

Pensieve::Pensieve(const WebSocketClient & client,
                   const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{
  string ipc_dir = "pensieve_ipc";
  fs::create_directory(ipc_dir);
  ipc_file_ = fs::path(ipc_dir) /
              (to_string(pid()) + "_" + to_string(client.connection_id()));

  IPCSocket sock;
  sock.set_reuseaddr();
  sock.bind(ipc_file_);
  sock.listen();

  fs::path ipc_path = fs::current_path() / ipc_file_;
  string pensieve_path;
  string nn_path;

  if (abr_config["pensieve_path"] && abr_config["nn_path"]) {
    pensieve_path = abr_config["pensieve_path"].as<string>();
    nn_path = abr_config["nn_path"].as<string>();
  } else {
    cerr << "Pensieve requires specifying paths in abr_config" << endl;
    throw runtime_error("Pensieve config missing");
  }

  /* start child process */
  vector<string> prog_args { pensieve_path, nn_path, ipc_path };

  pensieve_proc_ = make_unique<ChildProcess>(pensieve_path,
    [&pensieve_path, &prog_args]() {
      return ezexec(pensieve_path, prog_args);
    }
  );

  connection_ =  make_unique<FileDescriptor>(sock.accept());
}

Pensieve::~Pensieve()
{
  if (not fs::remove(ipc_file_)) {
    cerr << "Warning: file " << ipc_file_ << " cannot be removed" << endl;
  }
}

void Pensieve::video_chunk_acked(Chunk && c)
{
  const unsigned int size = c.size; // bytes
  const uint64_t trans_time = c.trans_time; // ms

  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  size_t vformats_cnt = vformats.size();

  assert(vformats_cnt == 10); // pensieve requires exactly 10 bitrates

  uint64_t next_vts = client_.next_vts().value();
  const auto & data_map = channel->vdata(next_vts);
  vector<double> next_chunk_sizes;

  for (size_t i = 0; i < vformats_cnt; i++) {
    const auto & vf = vformats[i];
    double chunk_size = get<1>(data_map.at(vf)); // bytes
    next_chunk_sizes.push_back(chunk_size);
  }

  sort(next_chunk_sizes.begin(), next_chunk_sizes.end());

  // TODO: increase trans_time to account for time to send audio chunks?
  json j;
  j["delay"] = trans_time; // ms
  j["playback_buf"] = client_.video_playback_buf(); // seconds
  j["rebuf_time"] = client_.cum_rebuffer(); // seconds
  j["last_chunk_size"] = (double)size; // bytes
  j["next_chunk_sizes"] = next_chunk_sizes; // bytes
  uint16_t json_len = j.dump().length();

  connection_->write(put_field(json_len) + j.dump());

  auto read_data = connection_->read_exactly(2); // read 2 byte length
  auto rcvd_json_len = get_uint16(read_data.data());
  auto read_json = connection_->read_exactly(rcvd_json_len);
  next_br_index_ = json::parse(read_json).at("bit_rate");
}

VideoFormat Pensieve::select_video_format()
{
  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  size_t vformats_cnt = vformats.size();

  uint64_t next_vts = client_.next_vts().value();
  const auto & data_map = channel->vdata(next_vts);
  vector<pair<double, size_t>> next_chunk_sizes; // store (chunk size, vf index)

  for (size_t i = 0; i < vformats_cnt; i++) {
    const auto & vf = vformats[i];
    double chunk_size = get<1>(data_map.at(vf));
    next_chunk_sizes.push_back(make_pair(chunk_size, i));
  }

  sort(next_chunk_sizes.begin(), next_chunk_sizes.end()); // sort by 1st element
  size_t next_format = get<1>(next_chunk_sizes[next_br_index_]);
  return client_.channel()->vformats()[next_format];
}
