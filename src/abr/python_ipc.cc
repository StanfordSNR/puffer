#include "python_ipc.hh"
#include "ws_client.hh"
#include "pid.hh"
#include "system_runner.hh"
#include "serialization.hh"
#include "ipc_socket.hh"
#include "json.hpp"
#include <tuple>
#include <vector>

using namespace std;
using json = nlohmann::json;

bool compare_vformats(const VideoFormat & l, const VideoFormat & r)
{
  // lower resolution and higher crf (lower quality)
  return make_tuple(l.width, l.height, -l.crf) < make_tuple(r.width, r.height, -r.crf);
}

PythonIPC::PythonIPC(const WebSocketClient & client,
                   const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{
  /* setup IPC connection */
  string ipc_dir = "python_ipc";
  fs::create_directory(ipc_dir);
  ipc_file_ = fs::path(ipc_dir) /
              (abr_name + "_"+ to_string(pid()) + "_" + to_string(client.connection_id()));

  IPCSocket sock;
  sock.set_reuseaddr();
  sock.bind(ipc_file_);
  sock.listen();

  /* initialize past chunk information */
  past_chunk_info_["delay"] = 0.;
  past_chunk_info_["ssim"] = 0.;
  past_chunk_info_["size"] = 0.;
  past_chunk_info_["cwnd"] = 0.;
  past_chunk_info_["in_flight"] = 0.;
  past_chunk_info_["min_rtt"] = 0.;
  past_chunk_info_["rtt"] = 0.;
  past_chunk_info_["delivery_rate"] = 0.;

  /* parse args for env process */
  fs::path ipc_path = fs::current_path() / ipc_file_;
  string test_path;
  string model_path;

  if (abr_config["test_path"] && abr_config["model_path"]) {
    test_path = abr_config["test_path"].as<string>();
    model_path = abr_config["model_path"].as<string>();
  } else {
    cerr << "PythonIPC requires specifying test file and model path in abr_config" << endl;
    throw runtime_error("PythonIPC config missing");
  }

  /* start abr env process */
  vector<string> prog_args {test_path, abr_name, model_path, ipc_path};

  python_proc_ = make_unique<ChildProcess>(test_path,
    [&test_path, &prog_args]() {
      return ezexec(test_path, prog_args);
    }
  );

  connection_ =  make_unique<FileDescriptor>(sock.accept());
}

PythonIPC::~PythonIPC()
{
  if (not fs::remove(ipc_file_)) {
    cerr << "Warning: file " << ipc_file_ << " cannot be removed" << endl;
  }
}

void PythonIPC::video_chunk_acked(Chunk && c)
{
  /* convert every unit of time to seconds
    and every unit of size to Mb */
  past_chunk_info_["delay"] = c.trans_time * 1e-3;            /* ms -> s */
  past_chunk_info_["ssim"] = c.ssim;                          /* unitless */
  past_chunk_info_["size"] = c.size * 1e-6;                   /* b -> Mb */
  past_chunk_info_["cwnd"] = c.cwnd;                          /* packets */
  past_chunk_info_["in_flight"] = c.in_flight;                /* packets */
  past_chunk_info_["min_rtt"] = c.min_rtt * 1e-6;             /* μs -> s */
  past_chunk_info_["rtt"] = c.rtt * 1e-6;                     /* μs -> s */
  past_chunk_info_["delivery_rate"] = c.delivery_rate * 1e-6; /* b/s -> Mb/s */
}

VideoFormat PythonIPC::select_video_format()
{
  const auto & channel = client_.channel();
  const unsigned int vduration = channel->vduration();
  const uint64_t next_ts = client_.next_vts().value();
  auto vformats = channel->vformats();
  size_t num_formats = vformats.size();

  assert(num_formats == ACTION_SPACE_N); // all the controllers expect the same action space
  sort(vformats.begin(), vformats.end(), &compare_vformats); // sort by increasing quality

  /* get future chunk sizes and ssims */
  vector<vector<double>> chunk_sizes(
    MAX_LOOKAHEAD_HORIZON, vector<double>(ACTION_SPACE_N, DEFAULT_CHUNK_SIZE));
  vector<vector<double>> chunk_ssims(
    MAX_LOOKAHEAD_HORIZON, vector<double>(ACTION_SPACE_N, DEFAULT_SSIM));

  size_t lookahead_horizon = min(
    MAX_LOOKAHEAD_HORIZON,
    (channel->vready_frontier().value() - next_ts) / vduration + 1);

  for (size_t i = 0; i < lookahead_horizon; i++) {
    const auto & data_map = channel->vdata(next_ts + vduration * i);

    for (size_t j = 0; j < num_formats; j++) {

      try {
        chunk_ssims[i][j] = channel->vssim(vformats[j], next_ts + vduration * i);
      } catch (const exception & e) {
        cerr << "Error occured when getting the ssim of "
             << next_ts + vduration * i << " " << vformats[j] << endl;
      }

      try {
        chunk_sizes[i][j] = get<1>(data_map.at(vformats[j])) * 1e-6; /* b -> Mb */
      } catch (const exception & e) {
        cerr << "Error occured when getting the size of "
             << next_ts + vduration * i << " " << vformats[j] << endl;
      }
    }
  }

  /* format json to send */
  json j;
  j["past_chunk"] = past_chunk_info_;
  j["buffer"] = client_.video_playback_buf(); // s
  j["cum_rebuf"] = client_.cum_rebuffer(); // s
  j["sizes"] = chunk_sizes; // Mb
  j["ssims"] = chunk_ssims; // unitless
  j["channel_name"] = channel->name(); // eg. fox, abc
  j["ts"] = next_ts; // timestamp
  uint16_t json_len = j.dump().length();

  connection_->write(put_field(json_len) + j.dump());

  /* read action received from ipc */
  auto read_data = connection_->read_exactly(2); // read 2 byte length
  auto rcvd_json_len = get_uint16(read_data.data());
  auto read_json = connection_->read_exactly(rcvd_json_len);
  size_t action = json::parse(read_json).at("action");

  return vformats[action];
}
