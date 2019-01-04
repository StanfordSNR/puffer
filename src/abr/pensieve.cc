#include "pensieve.hh"
#include "ws_client.hh"

using namespace std;

//TODO: Reference already existing put_field()
string put_field2(const uint16_t n)
{
  const uint16_t network_order = htobe16(n);
  return string(reinterpret_cast<const char *>(&network_order),
                sizeof(network_order));
}


Pensieve::Pensieve(const WebSocketClient & client,
         const string & abr_name, const YAML::Node & abr_config)
  : ABRAlgo(client, abr_name)
{

    cout << abr_config << endl; // TODO: Actually use abr_config or remove it
    // Config right now is done via constants in the Pensieve file, I am tempted
    // to leave it that way unless we think we will actually use different
    // configs for different people/channels.
}

void Pensieve::video_chunk_acked(const VideoFormat & format,
                            const double ssim,
                            const unsigned int size, //total length in bytes of chunk
                            const uint64_t trans_time) //ms
{
    const auto & channel = client_.channel();
    const auto & vformats = channel->vformats();
    size_t vformats_cnt = vformats.size();

    uint64_t next_vts = client_.next_vts().value();
    const auto & data_map = channel->vdata(next_vts);
    vector<pair<double, size_t>> next_chunk_sizes; //Store pairs of chunk size, corresponding vf index
    for (size_t i = 0; i < vformats_cnt; i++) {
      const auto & vf = vformats[i];

      size_t chunk_size = get<1>(data_map.at(vf)); //units?
      double chunk_size_mb = (((double)chunk_size) / 1000000); // MB //TODO: CHeck unit above
      next_chunk_sizes.push_back(make_pair(chunk_size_mb, i));
    }
    sort(next_chunk_sizes.begin(), next_chunk_sizes.end()); // sorts pairs by first element
    vector<double> next_chunk_sizes_bare; // just first element of next_chunk_sizes pair
    for (size_t i = 0; i < vformats_cnt; i++) {
      next_chunk_sizes_bare.push_back(get<0>(next_chunk_sizes[i]));
    }

    cout << "smallest sorted chunk size: " << get<0>(next_chunk_sizes[0]) << endl;
    cout << "largest sorted chunk size: " << get<0>(next_chunk_sizes[7]) << endl;
    // Use this to set delivery time for last video packet
    // Pensieve is going to consider all video packets as
    // also containing the audio associated with those
    // packets, which introduces some noise into these
    // values, which perhaps we should account for, but
    // currently do not.

    cout << "Video chunk acked!!" << format << ", SSIM: "<< ssim << endl;
    cout << "Last size per ACK: " << (double)(size) / 1000000 << endl;

    // Okay, so when a chunk is acked we should first notify Pensieve, then wait for
    // a response indicating what bit rate to use next
    // RESPOND
    // TODO: Increase trans_time to account for time to send audio chunks?
    cout << "PBUF: " << client_.video_playback_buf() << endl;
    //cout << "Last Chunk Size (MB): " << ((double)size) / 1000000 << endl;
    json j;
    j["delay"] = trans_time; // ms
    j["playback_buf"] = client_.video_playback_buf(); // seconds
    j["rebuf_time"] = client_.cum_rebuffer(); // cum seconds spent rebuffering up till now
    j["last_chunk_size"] = ((double)size) / 1000000; // MB
    j["next_chunk_sizes"] = next_chunk_sizes_bare; //MB
    uint16_t json_len = j.dump().length();
    cout << j.dump() << endl;

    connection_.write(put_field2(json_len) + j.dump());

    //Now, busy wait until Pensieve responds (on my laptop this takes between 1 and 4 ms)
    auto read_data = connection_.read();
    if (read_data.empty()) {
        cout << "Empty read, ERROR" << endl;
    } else {
        // TODO: Get this as JSON not raw
        // TODO: bitrate instead of index
        size_t index_in_bitrate_ladder = std::stoi( read_data );
        cout << "Index of next chunk to be sent: " << index_in_bitrate_ladder << endl;
        next_format_ = get<1>(next_chunk_sizes[index_in_bitrate_ladder]); //Works bc next_chunk_size is sorted
        cout << "next_format: " << vformats[next_format_] << endl;
    }
}

VideoFormat Pensieve::select_video_format()
{
  //reinit();
  // Basic design here is that the next_format is set after every video ack,
  // so any time this is called (which must come after an ack for the previous video chunk) this
  // value will have been updated.
  return client_.channel()->vformats()[next_format_];
}

/*
void Pensieve::reinit()
{
  curr_round_++;

  const auto & channel = client_.channel();
  const auto & vformats = channel->vformats();
  const unsigned int vduration = channel->vduration();
  const uint64_t curr_ts = *client_.next_vts() - vduration;

  chunk_length_ = (double) vduration / channel->timescale();
  num_formats_ = vformats.size();
  }
} */
