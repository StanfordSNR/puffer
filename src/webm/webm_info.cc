#include <iostream>
#include <memory>
#include "webm/callback.h"
#include "webm/file_reader.h"
#include "webm/status.h"
#include "webm/webm_parser.h"

#include "webm_info.hh"

using namespace webm;
using namespace std;

Status InfoCallback::OnTrackEntry (const ElementMetadata &,
                                   const TrackEntry & track_entry)
{
  auto audio = track_entry.audio.value();
  auto sampling_frequency = audio.sampling_frequency.value();
  info_.sample_rate = sampling_frequency;
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
  cout << "Sample rate: " << info_.sample_rate << " Hz" << endl;
}
