#ifndef WS_CLIENT_HH
#define WS_CLIENT_HH

#include <cstdint>
#include <optional>
#include <string>

#include "yaml.hh"
#include "channel.hh"

class MediaSegment
{
public:
  MediaSegment(mmap_t & data, std::optional<mmap_t> init);

  void read(std::string & dst, const size_t n);
  size_t offset() { return offset_; }
  size_t length() { return length_; }
  bool done() { return offset_ == length_; }

private:
  std::optional<mmap_t> init_;
  mmap_t data_;
  size_t offset_;
  size_t length_;
};

class VideoSegment : public MediaSegment
{
public:
  VideoSegment(const VideoFormat & format, mmap_t & data,
               std::optional<mmap_t> init);

  const VideoFormat & format() const { return format_; }

private:
  VideoFormat format_;
};

class AudioSegment : public MediaSegment
{
public:
  AudioSegment(const AudioFormat & format, mmap_t & data,
               std::optional<mmap_t> init);

  const AudioFormat & format() const { return format_; }

private:
  AudioFormat format_;
};

class WebSocketClient
{
public:
  WebSocketClient(const uint64_t connection_id);

  void init(const std::string & channel,
            const uint64_t vts, const uint64_t ats);

  /* accessors */
  uint64_t connection_id() const { return connection_id_; }
  std::optional<std::string> channel() const { return channel_; }

  std::optional<uint64_t> next_vts() const { return next_vts_; }
  std::optional<uint64_t> next_ats() const { return next_ats_; }

  std::optional<VideoSegment> & next_vsegment() { return next_vsegment_; }
  std::optional<AudioSegment> & next_asegment() { return next_asegment_; }

  double video_playback_buf() const { return video_playback_buf_; }
  double audio_playback_buf() const { return audio_playback_buf_; }

  std::optional<VideoFormat> curr_vq() const { return curr_vq_; }
  std::optional<AudioFormat> curr_aq() const { return curr_aq_; }

  std::optional<uint64_t> client_next_vts() const { return client_next_vts_; }
  std::optional<uint64_t> client_next_ats() const { return client_next_ats_; }

  unsigned int init_id() const { return init_id_; }

  /* mutators */
  void set_next_vts(const uint64_t next_vts) { next_vts_ = next_vts; }
  void set_next_ats(const uint64_t next_ats) { next_ats_ = next_ats; }

  void set_audio_playback_buf(const double buf) { audio_playback_buf_ = buf; }
  void set_video_playback_buf(const double buf) { video_playback_buf_ = buf; }

  void set_next_vsegment(const VideoFormat & format, mmap_t & data,
                         std::optional<mmap_t> & init);
  void clear_next_vsegment() { next_vsegment_.reset(); }

  void set_next_asegment(const AudioFormat & format, mmap_t & data,
                         std::optional<mmap_t> & init);
  void clear_next_asegment() { next_asegment_.reset(); }

  void set_curr_vq(const VideoFormat & quality) { curr_vq_ = quality; }
  void set_curr_aq(const AudioFormat & quality) { curr_aq_ = quality; }

  void set_client_next_vts(const uint64_t vts) { client_next_vts_ = vts; }
  void set_client_next_ats(const uint64_t ats) { client_next_ats_ = ats; }

private:
  uint64_t connection_id_ {};

  /* Fields set in init */
  std::optional<std::string> channel_ {};
  std::optional<uint64_t> next_vts_ {};
  std::optional<uint64_t> next_ats_ {};

  /* Segments in the process of being sent */
  std::optional<VideoSegment> next_vsegment_ {};
  std::optional<AudioSegment> next_asegment_ {};

  std::optional<VideoFormat> curr_vq_ {};
  std::optional<AudioFormat> curr_aq_ {};

  /* Fields from the client */
  double video_playback_buf_ {};
  double audio_playback_buf_ {};
  std::optional<uint64_t> client_next_vts_ {};
  std::optional<uint64_t> client_next_ats_ {};

  unsigned int init_id_ {0};
};

#endif /* WS_CLIENT_HH */
