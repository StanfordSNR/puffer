#include "channel.hh"

#include <fcntl.h>
#include <fstream>

#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

static const unsigned int DEFAULT_TIMESCALE = 90000;
static const unsigned int DEFAULT_VIDEO_DURATION = 180180;
static const unsigned int DEFAULT_AUDIO_DURATION = 432000;
static const string DEFAULT_VIDEO_CODEC = "video/mp4; codecs=\"avc1.42E020\"";
static const string DEFAULT_AUDIO_CODEC = "audio/webm; codecs=\"opus\"";
static const unsigned int DEFAULT_PRESENTATION_DELAY = 10;
static const unsigned int DEFAULT_CLEAN_WINDOW = 30;

Channel::Channel(const string & name, YAML::Node config, Inotify & inotify)
{
  live_ = config["live"].as<bool>();
  name_ = name;

  string input_dir = config["input"].as<string>();
  input_path_ = fs::path(input_dir);

  vformats_ = get_video_formats(config);
  aformats_ = get_audio_formats(config);

  timescale_ = config["timescale"] ?
      config["timescale"].as<unsigned int>() : DEFAULT_TIMESCALE;
  vduration_ = config["video_duration"] ?
      config["video_duration"].as<unsigned int>() : DEFAULT_VIDEO_DURATION;
  aduration_ = config["audio_duration"] ?
      config["audio_duration"].as<unsigned int>() : DEFAULT_AUDIO_DURATION;
  vcodec_ = config["video_codec"] ?
      config["video_codec"].as<string>() : DEFAULT_VIDEO_CODEC;
  acodec_ = config["audio_codec"] ?
      config["audio_codec"].as<string>() : DEFAULT_AUDIO_CODEC;

  if (live_) {
    presentation_delay_s_ = config["presentation_delay_s"] ?
        config["presentation_delay_s"].as<unsigned int>() :
        DEFAULT_PRESENTATION_DELAY;
    clean_window_s_ = config["clean_window_s"] ?
        config["clean_window_s"].as<unsigned int>() : DEFAULT_CLEAN_WINDOW;

    /* ensure an enough gap between clean_window_s and presentation_delay_s_ */
    if (presentation_delay_s_.value() + 5.0 * vduration_ / timescale_
        > clean_window_s_.value()) {
      throw runtime_error("clean_window_s should be larger enough "
                          "(5 video durations) than presentation_delay_s_");
    }
  } else {
    if (config["presentation_delay_s"] or config["clean_window_s"]) {
      throw runtime_error("presentation_delay_s_ or clean_window_s cannot be "
                          "specified if live is false");
    }
  }

  mmap_video_files(inotify);
  mmap_audio_files(inotify);
  load_ssim_files(inotify);

  if (not live_) {
    /* set init_vts_ to be the first ready timestamp */
    if (vdata_.cbegin() != vdata_.cend()) {
      uint64_t oldest_vts = vdata_.cbegin()->first;
      uint64_t oldest_ats = find_ats(oldest_vts);

      if (not vready(oldest_vts) or not aready(oldest_ats)) {
        throw runtime_error("VoD streaming is not ready");
      }

      init_vts_ = vdata_.cbegin()->first;
      cerr << "Channel " << name_ << ": ready to stream on demand" << endl;
    }
  } else {
    /* required to run update_live_edge again after all video and SSIM files
     * are mmaped, because they were not mmapped in ascending order */
    if (vdata_.crbegin() != vdata_.crend()) {
      uint64_t newest_vts = vdata_.crbegin()->first;
      update_live_edge(newest_vts);
    }
  }
}

optional<uint64_t> Channel::init_vts() const
{
  if (live_) {
    return vlive_frontier_;
  } else {
    return init_vts_;
  }
}

uint64_t Channel::find_ats(const uint64_t vts) const
{
  return (vts / aduration_) * aduration_;
}

bool Channel::ready() const
{
  return init_vts().has_value();
}

bool Channel::vready(const uint64_t ts) const
{
  auto it1 = vdata_.find(ts);
  if (it1 == vdata_.cend() or it1->second.size() != vformats_.size()) {
    return false;
  }
  auto it2 = vssim_.find(ts);
  if (it2 == vssim_.cend() or it2->second.size() != vformats_.size()) {
    return false;
  }

  return vinit_.size() == vformats_.size();
}

mmap_t & Channel::vinit(const VideoFormat & format)
{
  return vinit_.at(format);
}

mmap_t & Channel::vdata(const VideoFormat & format, const uint64_t ts)
{
  return vdata_.at(ts).at(format);
}

map<VideoFormat, mmap_t> & Channel::vdata(const uint64_t ts)
{
  return vdata_.at(ts);
}

double Channel::vssim(const VideoFormat & format, const uint64_t ts)
{
  return vssim_.at(ts).at(format);
}

map<VideoFormat, double> & Channel::vssim(const uint64_t ts)
{
  return vssim_.at(ts);
}

bool Channel::aready(const uint64_t ts) const
{
  auto it = adata_.find(ts);
  if (it == adata_.cend() or it->second.size() != aformats_.size()) {
    return false;
  }

  return ainit_.size() == aformats_.size();
}

mmap_t & Channel::ainit(const AudioFormat & format)
{
  return ainit_.at(format);
}

mmap_t & Channel::adata(const AudioFormat & format, const uint64_t ts)
{
  return adata_.at(ts).at(format);
}

map<AudioFormat, mmap_t> & Channel::adata(const uint64_t ts)
{
  return adata_.at(ts);
}

mmap_t mmap_file(const string & filepath)
{
  try {
    FileDescriptor fd(CheckSystemCall("open (" + filepath + ")",
                      open(filepath.c_str(), O_RDONLY)));
    size_t size = fd.filesize();
    shared_ptr<void> data = mmap_shared(nullptr, size, PROT_READ,
                                        MAP_PRIVATE, fd.fd_num(), 0);
    return {static_pointer_cast<char>(data), size};
  } catch (const exception & e) {
    print_exception("mmap_file", e);
    return {nullptr, 0};
  }
}

void Channel::munmap_video(const uint64_t newest_ts)
{
  assert(clean_window_s_);

  uint64_t clean_window_ts = clean_window_s_.value() * timescale_;
  if (newest_ts < clean_window_ts) return;
  uint64_t obsolete = newest_ts - clean_window_ts;

  optional<uint64_t> cleaned_ts;
  for (auto it = vdata_.cbegin(); it != vdata_.cend();) {
    uint64_t ts = it->first;
    if (ts <= obsolete) {
      cleaned_ts = ts;
      vssim_.erase(ts);
      it = vdata_.erase(it);
    } else {
      break;
    }
  }

  if (not cleaned_ts) return;

  if (not vclean_frontier_ or
      vclean_frontier_.value() < cleaned_ts.value()) {
    vclean_frontier_ = cleaned_ts.value();
  }
}

void Channel::munmap_audio(const uint64_t newest_ts)
{
  assert(clean_window_s_);

  uint64_t clean_window_ts = clean_window_s_.value() * timescale_;
  if (newest_ts < clean_window_ts) return;
  uint64_t obsolete = newest_ts - clean_window_ts;

  optional<uint64_t> cleaned_ts;
  for (auto it = adata_.cbegin(); it != adata_.cend();) {
    uint64_t ts = it->first;
    if (ts <= obsolete) {
      cleaned_ts = ts;
      it = adata_.erase(it);
    } else {
      break;
    }
  }

  if (not cleaned_ts) return;

  if (not aclean_frontier_ or
      aclean_frontier_.value() < cleaned_ts) {
    aclean_frontier_ = cleaned_ts.value();
  }
}

void Channel::update_live_edge(const uint64_t ts)
{
  assert(presentation_delay_s_);

  /* round up presentation delay to a multiple of video duration */
  uint64_t delay_vts = presentation_delay_s_.value() * timescale_;
  delay_vts = (delay_vts / vduration_ + 1) * vduration_;

  if (ts < delay_vts) return;

  uint64_t live_vts = ts - delay_vts;
  if (not vready(live_vts)) return;

  uint64_t live_ats = find_ats(live_vts);
  if (not aready(live_ats)) return;

  if (not vlive_frontier_) {
    cerr << "Channel " << name_ << ": ready to live stream" << endl;

    vlive_frontier_ = live_vts;
    alive_frontier_ = live_ats;
  } else if (live_vts > vlive_frontier_.value()) {
    vlive_frontier_ = live_vts;
    alive_frontier_ = live_ats;
  }
}

void Channel::do_mmap_video(const fs::path & filepath, const VideoFormat & vf)
{
  const mmap_t & data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    vinit_.emplace(vf, data_size);
  } else {
    if (filepath.extension() == ".m4s") {
      uint64_t ts = stoull(filestem);
      vdata_[ts][vf] = data_size;

      if (live_) {
        update_live_edge(ts);
        munmap_video(ts);

        /* assert that clean frontier < live frontier */
        if (vclean_frontier_ and vlive_frontier_ and
            vclean_frontier_.value() >= vlive_frontier_.value()) {
          cerr << "Error: video cleaner has caught up" << endl;
          vlive_frontier_.reset();
          alive_frontier_.reset();
        }
      }
    }
  }
}

void Channel::mmap_video_files(Inotify & inotify)
{
  for (const auto & vf : vformats_) {
    string video_dir = input_path_ / "ready" / vf.to_string();
    cerr << "Channel " << name_ << ": serve videos in " << video_dir << endl;

    /* watch new files only on live */
    if (live_) {
      inotify.add_watch(video_dir, IN_MOVED_TO,
        [this, &vf, video_dir](const inotify_event & event,
                               const string & path) {
          /* only interested in regular files that are moved into the dir */
          if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
            return;
          }

          assert(video_dir == path);
          assert(event.len != 0);

          fs::path filepath = fs::path(path) / event.name;
          do_mmap_video(filepath, vf);
        }
      );
    }

    /* process existing files */
    for (const auto & file : fs::directory_iterator(video_dir)) {
      do_mmap_video(file.path(), vf);
    }
  }
}

void Channel::do_mmap_audio(const fs::path & filepath, const AudioFormat & af)
{
  const mmap_t & data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    ainit_.emplace(af, data_size);
  } else {
    if (filepath.extension() == ".chk") {
      uint64_t ts = stoull(filestem);
      adata_[ts][af] = data_size;

      if (live_) {
        munmap_audio(ts);

        /* assert that clean frontier < live frontier */
        if (aclean_frontier_ and alive_frontier_ and
            aclean_frontier_.value() >= alive_frontier_.value()) {
          cerr << "Error: audio cleaner has caught up" << endl;
          vlive_frontier_.reset();
          alive_frontier_.reset();
        }
      }
    }
  }
}

void Channel::mmap_audio_files(Inotify & inotify)
{
  for (const auto & af : aformats_) {
    string audio_dir = input_path_ / "ready" / af.to_string();
    cerr << "Channel " << name_ << ": serve audios in " << audio_dir << endl;

    /* watch new files only on live */
    if (live_) {
      inotify.add_watch(audio_dir, IN_MOVED_TO,
        [this, &af, audio_dir](const inotify_event & event,
                               const string & path) {
          /* only interested in regular files that are moved into the dir */
          if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
            return;
          }

          assert(audio_dir == path);
          assert(event.len != 0);

          fs::path filepath = fs::path(path) / event.name;
          do_mmap_audio(filepath, af);
        }
      );
    }

    /* process existing files */
    for (const auto & file : fs::directory_iterator(audio_dir)) {
      do_mmap_audio(file.path(), af);
    }
  }
}

void Channel::do_read_ssim(const fs::path & filepath, const VideoFormat & vf) {
  if (filepath.extension() == ".ssim") {
    string filestem = filepath.stem();
    uint64_t ts = stoull(filestem);

    ifstream ssim_file(filepath);
    string line;
    getline(ssim_file, line);

    try {
      vssim_[ts][vf] = stod(line);
    } catch (const exception & e) {
      print_exception("invalid SSIM file", e);
      vssim_[ts][vf] = -1;
    }

    if (live_) {
      update_live_edge(ts);
    }
  }
}

void Channel::load_ssim_files(Inotify & inotify)
{
  for (const auto & vf : vformats_) {
    string ssim_dir = input_path_ / "ready" / (vf.to_string() + "-ssim");
    cerr << "Channel " << name_ << ": serve SSIMs in " << ssim_dir << endl;

    /* watch new files only on live */
    if (live_) {
      inotify.add_watch(ssim_dir, IN_MOVED_TO,
        [this, &vf, ssim_dir](const inotify_event & event,
                              const string & path) {
          /* only interested in regular files that are moved into the dir */
          if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
            return;
          }

          assert(ssim_dir == path);
          assert(event.len != 0);

          fs::path filepath = fs::path(path) / event.name;
          do_read_ssim(filepath, vf);
        }
      );
    }

    /* process existing files */
    for (const auto & file : fs::directory_iterator(ssim_dir)) {
      do_read_ssim(file.path(), vf);
    }
  }
}
