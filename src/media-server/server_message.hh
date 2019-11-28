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
  /* serialize server message: 16-bit length of msg_ | serialized msg_
   * video/audio chunk will be appended to serialized ServerMsg */
  std::string to_string() const;

protected:
  /* prevent this class from being instantiated */
  ServerMsg() {}

  json msg_ {};
};

class ServerInitMsg : public ServerMsg
{
public:
  ServerInitMsg(const unsigned int init_id,
                const std::string & channel,
                const std::string & video_codec,
                const std::string & audio_codec,
                const unsigned int timescale,
                const unsigned int vduration,
                const unsigned int aduration,
                const uint64_t init_vts,
                const uint64_t init_ats,
                const bool can_resume);
};

class ServerVideoMsg : public ServerMsg
{
public:
  ServerVideoMsg(const unsigned int init_id,
                 const std::string & channel,
                 const std::string & format,
                 const uint64_t timestamp,
                 const unsigned int byte_offset,
                 const unsigned int total_byte_length,
                 const double ssim);
};

class ServerAudioMsg : public ServerMsg
{
public:
  ServerAudioMsg(const unsigned int init_id,
                 const std::string & channel,
                 const std::string & format,
                 const uint64_t timestamp,
                 const unsigned int byte_offset,
                 const unsigned int total_byte_length);
};

class ServerErrorMsg : public ServerMsg
{
public:
  enum class Type {
    Maintenance, /* server is under maintenance */
    Reinit,      /* channel needs to be reinitialized */
    Unavailable, /* channel is not available */
    Limit        /* limit on number of concurrent viewers */
  };

  ServerErrorMsg(const unsigned int init_id, const Type error_type);
};

class MediaSegment
{
public:
  /* read up to n bytes from init_ (if exists) and data_ and append to dst */
  void read(std::string & dst, const size_t n);

  /* length of init_ (if exists) and data_ */
  size_t length() { return length_; }

  /* byte offset in length_ */
  size_t offset() { return offset_; }

  /* all bytes have been read */
  bool done() { return offset_ == length_; }

protected:
  /* prevent this class from being instantiated */
  MediaSegment(const mmap_t & data, const std::optional<mmap_t> & init);

private:
  mmap_t data_;
  std::optional<mmap_t> init_;
  size_t length_;
  size_t offset_;
};

class VideoSegment : public MediaSegment
{
public:
  VideoSegment(const VideoFormat & format,
               const mmap_t & data,
               const std::optional<mmap_t> & init);

  VideoFormat format() const { return format_; }

private:
  VideoFormat format_;
};

class AudioSegment : public MediaSegment
{
public:
  AudioSegment(const AudioFormat & format,
               const mmap_t & data,
               const std::optional<mmap_t> & init);

  AudioFormat format() const { return format_; }

private:
  AudioFormat format_;
};

#endif /* SERVER_MESSAGE_HH */
