#include "server_message.hh"

using namespace std;

string ServerMsg::type_str()
{
  switch (type_) {
    case Type::Unknown:
      throw runtime_error("Cannot convert an unknown type to a string");
    case Type::Init:
      return "server-init";
    case Type::Video:
      return "server-video";
    case Type::Audio:
      return "server-audio";
    default:
      throw runtime_error("Invalid type");
  }
}

string ServerMsg::to_string()
{
  string msg_str = msg_.dump();
  uint16_t msg_len = msg_str.length();
  string ret(sizeof(uint16_t) + msg_len, 0);

  /* Network endian */
  uint16_t msg_len_be = htobe16(msg_len);
  memcpy(&ret[0], &msg_len_be, sizeof(uint16_t));

  copy(msg_str.begin(), msg_str.end(), ret.begin() + sizeof(uint16_t));
  return ret;
}

ServerInitMsg::ServerInitMsg(const string & channel,
                             const string & video_codec,
                             const string & audio_codec,
                             const unsigned int timescale,
                             const uint64_t init_vts,
                             const uint64_t init_ats,
                             const unsigned int init_id,
                             const bool can_resume)
{
  set_type(Type::Init);
  set_msg({
    {"type", type_str()},
    {"channel", channel},
    {"videoCodec", video_codec},
    {"audioCodec", audio_codec},
    {"timescale", timescale},
    {"initVideoTimestamp", init_vts},
    {"initAudioTimestamp", init_ats},
    {"initId", init_id},
    {"canResume", can_resume}
  });
}

ServerVideoMsg::ServerVideoMsg(const string & channel,
                               const string & quality,
                               const double ssim,
                               const uint64_t timestamp,
                               const unsigned int duration,
                               const unsigned int byte_offset,
                               const unsigned int total_byte_length)
{
  set_type(Type::Video);
  set_msg({
    {"type", type_str()},
    {"channel", channel},
    {"quality", quality},
    {"ssim", ssim},
    {"timestamp", timestamp},
    {"duration", duration},
    {"byteOffset", byte_offset},
    {"totalByteLength", total_byte_length}
  });
}

ServerAudioMsg::ServerAudioMsg(const string & channel,
                               const string & quality,
                               const uint64_t timestamp,
                               const unsigned int duration,
                               const unsigned int byte_offset,
                               const unsigned int total_byte_length)
{
  set_type(Type::Audio);
  set_msg({
    {"type", type_str()},
    {"channel", channel},
    {"quality", quality},
    {"timestamp", timestamp},
    {"duration", duration},
    {"byteOffset", byte_offset},
    {"totalByteLength", total_byte_length}
  });
}

MediaSegment::MediaSegment(const mmap_t & data,
                           const optional<mmap_t> & init)
  : data_(data), init_(init), offset_(0), length_()
{
  length_ = get<1>(data_);
  if (init_) {
    length_ += get<1>(*init_);
  }
}

void MediaSegment::read(string & dst, const size_t n)
{
  assert(n > 0);
  assert(offset_ < length_);

  const size_t init_size = init_ ? get<1>(*init_) : 0;
  const size_t orig_dst_len = dst.length();

  if (init_ and offset_ < init_size) {
    const size_t to_read = init_size - offset_ > n ? n : init_size - offset_;
    dst.append(get<0>(*init_).get() + offset_, to_read);
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

VideoSegment::VideoSegment(const VideoFormat & format,
                           const mmap_t & data,
                           const optional<mmap_t> & init)
  : MediaSegment(data, init), format_(format)
{}

AudioSegment::AudioSegment(const AudioFormat & format,
                           const mmap_t & data,
                           const optional<mmap_t> & init)
  : MediaSegment(data, init), format_(format)
{}
