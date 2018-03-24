#include "ws_client.hh"

using namespace std;

MediaSegment::MediaSegment(mmap_t & data, optional<mmap_t> init)
  : init_(init), data_(data), offset_(0), length_()
{
  length_ = get<1>(data_);
  if (init_.has_value()) {
    length_ += get<1>(init_.value());
  }
}

void MediaSegment::read(string & dst, const size_t n)
{
  assert(offset_ < length_);
  const size_t init_size = init_.has_value() ? get<1>(init_.value()) : 0;
  const size_t orig_dst_len = dst.length();

  if (init_.has_value() and offset_ < init_size) {
    const size_t to_read = init_size - offset_ > n ? n : init_size - offset_;
    dst.append(get<0>(init_.value()).get() + offset_, to_read);
    offset_ += to_read;
    if (dst.length() - orig_dst_len >= n) {
      return;
    }
  }

  const auto & [seg_data, seg_size] = data_;
  const size_t offset_into_data = offset_ - init_size;

  size_t to_read = n - (dst.length() - orig_dst_len);
  to_read = seg_size - offset_into_data > to_read ?
            to_read : seg_size - offset_into_data;

  dst.append(seg_data.get() + offset_into_data, to_read);
  offset_ += to_read;

  assert(dst.length() - orig_dst_len <= n);
}

VideoSegment::VideoSegment(const VideoFormat & format, mmap_t & data,
                           optional<mmap_t> init)
  : MediaSegment(data, init), format_(format)
{}

AudioSegment::AudioSegment(const AudioFormat & format, mmap_t & data,
                           optional<mmap_t> init)
  : MediaSegment(data, init), format_(format)
{}

WebSocketClient::WebSocketClient(const uint64_t connection_id)
{
  connection_id_ = connection_id;
}

void WebSocketClient::init(const string & channel,
                           const uint64_t vts, const uint64_t ats)
{
  channel_ = channel;
  next_vts_ = vts;
  next_ats_ = ats;

  next_vsegment_.reset();
  next_asegment_.reset();

  curr_vq_.reset();
  curr_aq_.reset();

  video_playback_buf_ = 0;
  audio_playback_buf_ = 0;
  client_next_vts_ = vts;
  client_next_ats_ = ats;

  init_id_++;
}

void WebSocketClient::set_next_vsegment(const VideoFormat & format,
                                        mmap_t & data, optional<mmap_t> & init)
{
  next_vsegment_ = {format, data, init};
}

void WebSocketClient::set_next_asegment(const AudioFormat & format,
                                        mmap_t & data, optional<mmap_t> & init)
{
  next_asegment_ = {format, data, init};
}
