#ifndef SERVER_MESSAGE_HH
#define SERVER_MESSAGE_HH

#include <cstdint>
#include <string>
#include <vector>

#include "channel.hh"
#include "json.hpp"
using json = nlohmann::json;

class ServerMsg
{
public:
  enum class Type {
    Unknown,
    Init,
    Video,
    Audio
  };

  Type type() { return type_; }
  std::string type_str();

  /* serialize server message: "length(msg_)|serialized(msg_)" */
  std::string to_string();

protected:
  ServerMsg() {}

  /* mutators */
  void set_msg(json && msg) { msg_ = move(msg); }
  void set_type(const Type type) { type_ = type; }

private:
  json msg_ {};
  Type type_ {Type::Unknown};
};

class ServerInitMsg : public ServerMsg
{
public:
  ServerInitMsg(const std::string & channel,
                const std::string & video_codec,
                const std::string & audio_codec,
                const unsigned int timescale,
                const uint64_t init_vts,
                const uint64_t init_ats,
                const unsigned int init_id,
                const bool can_resume);
};

class ServerVideoMsg : public ServerMsg
{
public:
  ServerVideoMsg(const std::string & channel,
                 const std::string & quality,
                 const double ssim,
                 const uint64_t timestamp,
                 const unsigned int duration,
                 const unsigned int byte_offset,
                 const unsigned int total_byte_length);
};

class ServerAudioMsg : public ServerMsg
{
public:
  ServerAudioMsg(const std::string & channel,
                 const std::string & quality,
                 const uint64_t timestamp,
                 const unsigned int duration,
                 const unsigned int byte_offset,
                 const unsigned int total_byte_length);
};

class MediaSegment
{
public:
  MediaSegment(const mmap_t & data, const std::optional<mmap_t> & init);

  void read(std::string & dst, const size_t n);
  size_t offset() { return offset_; }
  size_t length() { return length_; }
  bool done() { return offset_ == length_; }

private:
  mmap_t data_;
  std::optional<mmap_t> init_;
  size_t offset_;
  size_t length_;
};

class VideoSegment : public MediaSegment
{
public:
  VideoSegment(const VideoFormat & format,
               const mmap_t & data,
               const std::optional<mmap_t> & init);

  const VideoFormat & format() const { return format_; }

private:
  VideoFormat format_;
};

class AudioSegment : public MediaSegment
{
public:
  AudioSegment(const AudioFormat & format,
               const mmap_t & data,
               const std::optional<mmap_t> & init);

  const AudioFormat & format() const { return format_; }

private:
  AudioFormat format_;
};

#endif /* SERVER_MESSAGE_HH */
