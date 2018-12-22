#include "server_message.hh"

#include "strict_conversions.hh"

using namespace std;

string ServerMsg::to_string() const
{
  string msg_str = msg_.dump();
  uint16_t msg_len = narrow_cast<uint16_t>(msg_str.length());
  string ret(sizeof(uint16_t) + msg_len, 0);

  /* Network endian */
  uint16_t msg_len_be = htobe16(msg_len);
  memcpy(&ret[0], &msg_len_be, sizeof(uint16_t));
  copy(msg_str.begin(), msg_str.end(), ret.begin() + sizeof(uint16_t));

  return ret;
}

ServerInitMsg::ServerInitMsg(const unsigned int init_id,
                             const string & channel,
                             const string & video_codec,
                             const string & audio_codec,
                             const unsigned int timescale,
                             const unsigned int vduration,
                             const unsigned int aduration,
                             const uint64_t init_vts,
                             const uint64_t init_ats,
                             const bool can_resume)
{
  msg_ = {
    {"type", "server-init"},
    {"initId", init_id},
    {"channel", channel},
    {"videoCodec", video_codec},
    {"audioCodec", audio_codec},
    {"timescale", timescale},
    {"videoDuration", vduration},
    {"audioDuration", aduration},
    {"initVideoTimestamp", init_vts},
    {"initAudioTimestamp", init_ats},
    {"canResume", can_resume}
  };
}

ServerVideoMsg::ServerVideoMsg(const unsigned int init_id,
                               const string & channel,
                               const string & format,
                               const uint64_t timestamp,
                               const unsigned int byte_offset,
                               const unsigned int total_byte_length,
                               const double ssim)
{
  msg_ = {
    {"type", "server-video"},
    {"initId", init_id},
    {"channel", channel},
    {"format", format},
    {"timestamp", timestamp},
    {"byteOffset", byte_offset},
    {"totalByteLength", total_byte_length},
    {"ssim", ssim}
  };
}

ServerAudioMsg::ServerAudioMsg(const unsigned int init_id,
                               const string & channel,
                               const string & format,
                               const uint64_t timestamp,
                               const unsigned int byte_offset,
                               const unsigned int total_byte_length)
{
  msg_ = {
    {"type", "server-audio"},
    {"initId", init_id},
    {"channel", channel},
    {"format", format},
    {"timestamp", timestamp},
    {"byteOffset", byte_offset},
    {"totalByteLength", total_byte_length}
  };
}

ServerErrorMsg::ServerErrorMsg(const unsigned int init_id,
                               const Type error_type)
{
  string error_type_str;
  string error_message;

  if (error_type == Type::Maintenance) {
    error_type_str = "maintenance";
    error_message = "Sorry, Puffer is down for maintenance right now. "
      "Please try again later.";
  } else if (error_type == Type::Reinit) {
    error_type_str = "reinit";
    error_message = "Your connection is going to be reset.";
  } else if (error_type == Type::Unavailable) {
    error_type_str = "unavailable";
    error_message = "Sorry, the channel is not currently available. "
      "Please try another channel or refresh the page.";
  }

  msg_ = {
    {"type", "server-error"},
    {"initId", init_id},
    {"errorType", error_type_str},
    {"errorMessage", error_message}
  };
}

MediaSegment::MediaSegment(const mmap_t & data,
                           const optional<mmap_t> & init)
  : data_(data), init_(init), length_(), offset_(0)
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
