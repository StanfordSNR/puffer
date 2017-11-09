#ifndef WEBM_INFO_HH
#define WEBM_INFO_HH

#include <string>
#include <memory>

#include "webm/callback.h"
#include "webm/file_reader.h"
#include "webm/status.h"
#include "webm/webm_parser.h"

namespace webm
{

struct AudioInfo
{
  uint32_t timescale = 0;
  uint32_t duration = 0;
  uint32_t sample_rate = 0;
  uint32_t bitrate = 0;
};

class InfoCallback : public Callback
{
public:
  webm::Status OnTrackEntry(const webm::ElementMetadata & metadata,
                      const webm::TrackEntry & track_entry) override;
  InfoCallback(AudioInfo & info) : info_(info)
  {}

private:
  AudioInfo & info_;
};

class WebmInfo
{
public:
  /*
  std::tuple<uint32_t, uint32_t> get_timescale_duration();
  uint32_t get_bitrate();
  uint32_t get_bitrate(uint32_t timescale, uint32_t duration);
  uint32_t get_sample_rate();
  */
  WebmInfo(const std::string & filename);

  /* It's a mess to have File*, but we have no other choice */
  WebmInfo(WebmInfo & other)
    : file_(std::move(other.file_)), parser_(std::move(other.parser_)),
      reader_(std::move(other.reader_)), info_(std::move(other.info_))
  {}

  WebmInfo & operator=(WebmInfo & other);
  void print_info();

private:
  FILE * file_;
  std::shared_ptr<WebmParser> parser_;
  std::shared_ptr<FileReader> reader_;
  AudioInfo info_;
};
}

#endif /* WEBM_INFO_HH */
