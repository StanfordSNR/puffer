#include "pensieve.hh"
#include "ws_client.hh"
#include "pid.hh"
#include "child_process.hh"
#include "system_runner.hh"
#include "serialization.hh"
#include <random>

using namespace std;

Pensieve::Pensieve(const WebSocketClient & client,
         const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{

  string ipc_dir = "pensieve_ipc";
  fs::create_directories(ipc_dir);
  fs::path ipc_file_ = ipc_dir / fs::path(to_string(pid()) + "_"  +
                                          to_string(client.connection_id()));
  IPCSocket sock;
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
    throw "Config Error";
  }

  /* Start Child Process */
  vector<string> prog_args { pensieve_path, nn_path, ipc_path };

  pensieve_proc_ = make_unique<ChildProcess>(pensieve_path,
                                             [&pensieve_path, &prog_args]() {
      return ezexec(pensieve_path, prog_args);
    }
  );

  connection_ =  sock.accept();
}

Pensieve::~Pensieve() {
  error_code ec;
  if (not fs::remove(ipc_file_, ec)) {
    cerr << "Warning: file " << ipc_file_ << " cannot be removed" << endl;
  }
}

void Pensieve::video_chunk_acked(const VideoFormat &,
                            const double,
                            const unsigned int size, //total length in bytes of chunk
                            const uint64_t trans_time) //ms
{
  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  size_t vformats_cnt = vformats.size();

  assert(vformats_cnt == 10); // Pensieve requires exactly 10 bitrates

  uint64_t next_vts = client_.next_vts().value();
  const auto & data_map = channel->vdata(next_vts);
  vector<double> next_chunk_sizes; // Store pairs of chunk size, corresponding vf index
  for (size_t i = 0; i < vformats_cnt; i++) {
    const auto & vf = vformats[i];
    double chunk_size = (double)get<1>(data_map.at(vf)); // Bytes
    next_chunk_sizes.push_back(chunk_size);
  }
  sort(next_chunk_sizes.begin(), next_chunk_sizes.end());

  // TODO: Increase trans_time to account for time to send audio chunks?
  json j;
  j["delay"] = trans_time; // ms
  j["playback_buf"] = client_.video_playback_buf(); // seconds
  j["rebuf_time"] = client_.cum_rebuffer(); // cum seconds spent rebuffering up till now
  j["last_chunk_size"] = (double)size; // Bytes
  j["next_chunk_sizes"] = next_chunk_sizes; //Bytes
  uint16_t json_len = j.dump().length();
  cout << j.dump() << endl;

  connection_.write(put_field(json_len) + j.dump());

  auto read_data = connection_.read_exactly(2); //read 2 byte length
  auto rcvd_json_len = get_uint16( read_data.data() );
  auto read_json = connection_.read_exactly(rcvd_json_len);
  next_br_index_ = json::parse(read_json)["bit_rate"];
  cout << "Index of next chunk to be sent: " << next_br_index_ << endl;
}

VideoFormat Pensieve::select_video_format()
{
  // Basic design here is that the next_format is set after every video ack,
  // so any time this is called (which must come after an ack for the previous video chunk) this
  // value will have been updated.
  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  size_t vformats_cnt = vformats.size();

  uint64_t next_vts = client_.next_vts().value();
  const auto & data_map = channel->vdata(next_vts);
  vector<pair<double, size_t>> next_chunk_sizes; //Store pairs of chunk size, corresponding vf index
  for (size_t i = 0; i < vformats_cnt; i++) {
    const auto & vf = vformats[i];

    double chunk_size = (double)get<1>(data_map.at(vf)); //Bytes
    next_chunk_sizes.push_back(make_pair(chunk_size, i));
  }
  sort(next_chunk_sizes.begin(), next_chunk_sizes.end()); // sorts pairs by first element
  size_t next_format = get<1>(next_chunk_sizes[next_br_index_]); //Works bc next_chunk_size is sorted
  return client_.channel()->vformats()[next_format];
}
