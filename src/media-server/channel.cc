#include "channel.hh"

#include <fcntl.h>
#include <fstream>
#include <algorithm>

#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

static const unsigned int DEFAULT_TIMESCALE = 90000;
static const unsigned int DEFAULT_VIDEO_DURATION = 180180;  // 2.002s per chunk
static const unsigned int DEFAULT_AUDIO_DURATION = 432000;  // 4.8s per chunk
static const string DEFAULT_VIDEO_CODEC = "video/mp4; codecs=\"avc1.42E020\"";
static const string DEFAULT_AUDIO_CODEC = "audio/webm; codecs=\"opus\"";
static const unsigned int DEFAULT_PRESENT_DELAY_CHUNK = 10;  // 10 chunks
static const unsigned int PRESENT_CLEAN_DIFF = 10;  // 10 chunks

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
    present_delay_chunk_ = config["present_delay_chunk"] ?
        config["present_delay_chunk"].as<unsigned int>() :
        DEFAULT_PRESENT_DELAY_CHUNK;
    clean_window_chunk_ = *present_delay_chunk_ + PRESENT_CLEAN_DIFF;
  } else {
    if (config["present_delay_chunk"]) {
      throw runtime_error("present_delay_chunk can't be set if live is false");
    }
  }

  mmap_video_files(inotify);
  mmap_audio_files(inotify);
  load_ssim_files(inotify);

  if (not live_) {
    /* set init_vts_ to be the first ready timestamp */
    if (vdata_.cbegin() != vdata_.cend()) {
      uint64_t oldest_vts = vdata_.cbegin()->first;
      uint64_t oldest_ats = floor_ats(oldest_vts);

      if (not vready(oldest_vts) or not aready(oldest_ats)) {
        throw runtime_error("VoD streaming is not ready");
      }

      init_vts_ = vdata_.cbegin()->first;
      cerr << "Channel " << name_ << ": ready to stream on demand" << endl;
    }
  }
}

optional<uint64_t> Channel::init_vts() const
{
  if (live_) {
    return live_edge();
  } else {
    return init_vts_;
  }
}

optional<uint64_t> Channel::init_ats() const
{
  if (not init_vts()) {
    return nullopt;
  } else {
    return floor_ats(*init_vts());
  }
}

uint64_t Channel::floor_vts(const uint64_t ts) const
{
  return (ts / vduration_) * vduration_;
}

uint64_t Channel::floor_ats(const uint64_t ts) const
{
  return (ts / aduration_) * aduration_;
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

  return true;
}

const mmap_t & Channel::vinit(const VideoFormat & format) const
{
  return vinit_.at(format);
}

const mmap_t & Channel::vdata(const VideoFormat & format,
                              const uint64_t ts) const
{
  return vdata_.at(ts).at(format);
}

const map<VideoFormat, mmap_t> & Channel::vdata(const uint64_t ts) const
{
  return vdata_.at(ts);
}

double Channel::vssim(const VideoFormat & format, const uint64_t ts) const
{
  return vssim_.at(ts).at(format);
}

const map<VideoFormat, double> & Channel::vssim(const uint64_t ts) const
{
  return vssim_.at(ts);
}

bool Channel::aready(const uint64_t ts) const
{
  auto it = adata_.find(ts);
  if (it == adata_.cend() or it->second.size() != aformats_.size()) {
    return false;
  }

  return true;
}

const mmap_t & Channel::ainit(const AudioFormat & format) const
{
  return ainit_.at(format);
}

const mmap_t & Channel::adata(const AudioFormat & format,
                              const uint64_t ts) const
{
  return adata_.at(ts).at(format);
}

const map<AudioFormat, mmap_t> & Channel::adata(const uint64_t ts) const
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

void Channel::munmap_video(const uint64_t ts)
{
  uint64_t clean_window_ts = (clean_window_chunk_.value() - 1) * vduration_;
  if (ts < clean_window_ts) return;
  uint64_t obsolete = ts - clean_window_ts;

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

  if (not vclean_frontier_ or *vclean_frontier_ < *cleaned_ts) {
    vclean_frontier_ = *cleaned_ts;
  }
}

void Channel::munmap_audio(const uint64_t ts)
{
  uint64_t clean_window_ts = (clean_window_chunk_.value() - 1) * vduration_;
  if (ts < clean_window_ts) return;
  uint64_t obsolete = ts - clean_window_ts;

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

  if (not aclean_frontier_ or *aclean_frontier_ < *cleaned_ts) {
    aclean_frontier_ = *cleaned_ts;
  }
}

optional<uint64_t> Channel::live_edge() const
{
  /* init files are not ready */
  if (vinit_.size() != vformats_.size() or
      ainit_.size() != aformats_.size()) {
    return nullopt;
  }

  /* no video chunks or no audio chunks are ready */
  if (not vready_frontier_ or not aready_frontier_) {
    return nullopt;
  }

  /* find the max ready video chunk, such that its corresponding ("floor_ats")
   * audio chunk is also ready */
  uint64_t ready_frontier = *vready_frontier_;
  while (not aready(floor_ats(ready_frontier))) {
    if (ready_frontier < vduration_) {
      return nullopt;
    }

    ready_frontier -= vduration_;
  }

  uint64_t delay_vts = (present_delay_chunk_.value() - 1) * vduration_;
  if (ready_frontier < delay_vts) {
    return nullopt;
  }

  return ready_frontier - delay_vts;
}

void Channel::update_vready_frontier(const uint64_t vts)
{
  if (not vready(vts)) return;

  /* update vready_frontier_ */
  if (not vready_frontier_) {
    vready_frontier_ = vts;
  } else {
    /* vready_frontier_ is a contiguous range of ready video chunks */
    if (vts == *vready_frontier_ + vduration_) {
      vready_frontier_ = *vready_frontier_ + vduration_;

      while (vready(*vready_frontier_ + vduration_)) {
        vready_frontier_ = *vready_frontier_ + vduration_;
      }
    }
  }
}

void Channel::update_aready_frontier(const uint64_t ats)
{
  if (not aready(ats)) return;

  /* update aready_frontier_ */
  if (not aready_frontier_) {
    aready_frontier_ = ats;
  } else {
    /* aready_frontier_ is a contiguous range of ready audio chunks */
    if (ats == *aready_frontier_ + aduration_) {
      aready_frontier_ = *aready_frontier_ + aduration_;

      while (aready(*aready_frontier_ + aduration_)) {
        aready_frontier_ = *aready_frontier_ + aduration_;
      }
    }
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
        update_vready_frontier(ts);

        if (vready_frontier_) {
          munmap_video(*vready_frontier_);
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
        update_aready_frontier(ts);

        if (aready_frontier_) {
          munmap_audio(*aready_frontier_);
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
      update_vready_frontier(ts);
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
