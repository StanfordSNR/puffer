#include <iostream>
#include <memory>
#include "webm/callback.h"
#include "webm/file_reader.h"
#include "webm/status.h"
#include "webm/webm_parser.h"

#include "webm_info.hh"

using namespace webm;
using namespace std;

Status InfoCallback::OnTrackEntry(const ElementMetadata &,
                                   const TrackEntry & track_entry)
{
  auto audio = track_entry.audio.value();
  auto sampling_frequency = audio.sampling_frequency.value();
  info_.sample_rate = sampling_frequency;
  return Status(Status::kOkCompleted);
}

Status InfoCallback::OnInfo(const ElementMetadata & , const Info & info)
{
  info_.timescale = info.timecode_scale.value();
  info_.duration = info.duration.value();
  return Status(Status::kOkCompleted);
}

Status InfoCallback::OnClusterBegin(
      const ElementMetadata & metadata, const Cluster & ,
      Action * action)
{
  info_.size += metadata.size;
  *action = Action::kRead;
  return Status(Status::kOkCompleted);
}

WebmInfo::WebmInfo(
    const string & filename)
  : file_(), parser_(), reader_(), info_()
{
    file_ = fopen(filename.c_str(), "rb");
    reader_ = make_shared<FileReader>(file_);
    parser_ = make_shared<WebmParser>();
    InfoCallback callback(info_);
    Status status = parser_->Feed(&callback, reader_.get());
    if (!status.completed_ok()) {
      throw runtime_error("parsing error for " + filename);
    }
}

WebmInfo & WebmInfo::operator=(WebmInfo & other)
{
  if (this != &other) {
    file_ = other.file_;
    parser_ = other.parser_;
    reader_ = other.reader_;
  }

  return *this;
}

void WebmInfo::print_info()
{
  cout << "Sample rate: " << info_.sample_rate << " Hz" << endl
       << "Timescale:   " << info_.timescale << endl
       << "Duration:    " << ((double)info_.duration / 1000) << " s" << endl
       << "Size:        " << info_.size / 1000 << " kB" << endl
       << "Bitrate:     " << get_bitrate() / 1000 << " kbps" << endl;
}

pair<uint32_t, uint32_t> WebmInfo::get_timescale_duration()
{
  /* convert duration to the timescale ticks */
  uint32_t duration = info_.timescale * info_.duration;
  return make_pair(info_.timescale, duration);
}

uint32_t WebmInfo::get_bitrate()
{
  double size = static_cast<double>(info_.size);
  double d_bitrate = size / info_.duration * 1000 * 8;
  uint32_t bitrate = static_cast<uint32_t>(d_bitrate);
  return (bitrate / 1000) * 1000;
}
