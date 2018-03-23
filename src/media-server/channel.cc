#include "channel.hh"

#include <fcntl.h>
#include <fstream>
#include <sstream>

#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

Channel::Channel(const string & name, YAML::Node config, Inotify & inotify)
{
  live_ = config["live"].as<bool>();
  name_ = name;

  string input_dir = config["input"].as<string>();
  input_path_ = fs::path(input_dir);

  vformats_ = get_video_formats(config);
  aformats_ = get_audio_formats(config);

  timescale_ = config["timescale"].as<unsigned int>();
  vduration_ = config["video_duration"].as<unsigned int>();
  aduration_ = config["audio_duration"].as<unsigned int>();
  vcodec_ = config["video_codec"].as<string>();
  acodec_ = config["audio_codec"].as<string>();

  if (live_) {
    presentation_delay_s_ = config["presentation_delay_s"].as<unsigned int>();
    clean_window_s_ = config["clean_window_s"].as<unsigned int>();

    /* ensure an enough gap between clean_window_s and presentation_delay_s_ */
    if (presentation_delay_s_.value() + 5.0 * vduration_ / timescale_
        <= clean_window_s_.value()) {
      throw runtime_error("clean_window_s should be larger enough "
                          "(5 video durations) than presentation_delay_s_");
    }

    if (config["init_vts"]) {
      throw runtime_error("init_vts cannot be specified if live is true");
    }
  } else {
    init_vts_ = config["init_vts"].as<uint64_t>();

    if (not is_valid_vts(init_vts_.value())) {
      throw runtime_error("invalid init_vts: should be a multiple of video "
                          "duration");
    }

    if (config["presentation_delay_s"] or config["clean_window_s"]) {
      throw runtime_error("presentation_delay_s_ or clean_window_s cannot be "
                          "specified if live is false");
    }
  }

  mmap_video_files(inotify);
  mmap_audio_files(inotify);
  load_ssim_files(inotify);
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

bool Channel::vready(const uint64_t ts) const
{
  auto it1 = vdata_.find(ts);
  if (it1 == vdata_.end() or it1->second.size() != vformats_.size()) {
    return false;
  }
  auto it2 = vssim_.find(ts);
  if (it2 == vssim_.end() or it2->second.size() != vformats_.size()) {
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
  if (it == adata_.end() or it->second.size() != aformats_.size()) {
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

mmap_t mmap_file(const string & filepath)
{
  FileDescriptor fd(CheckSystemCall("open (" + filepath + ")",
                    open(filepath.c_str(), O_RDONLY)));
  size_t size = fd.filesize();
  shared_ptr<void> data = mmap_shared(nullptr, size, PROT_READ,
                                      MAP_PRIVATE, fd.fd_num(), 0);
  return {static_pointer_cast<char>(data), size};
}

void Channel::munmap_video(const uint64_t newest_ts)
{
  assert(clean_window_s_.has_value());

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

  if (not cleaned_ts.has_value()) return;

  if (not vclean_frontier_.has_value() or
      vclean_frontier_.value() < cleaned_ts.value()) {
    vclean_frontier_ = cleaned_ts.value();
  }
}

void Channel::munmap_audio(const uint64_t newest_ts)
{
  assert(clean_window_s_.has_value());

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

  if (not cleaned_ts.has_value()) return;

  if (not aclean_frontier_.has_value() or
      aclean_frontier_.value() < cleaned_ts) {
    aclean_frontier_ = cleaned_ts.value();
  }
}

void Channel::update_live_edge(const uint64_t ts)
{
  assert(presentation_delay_s_.has_value());

  /* round up presentation delay to a multiple of video duration */
  uint64_t delay_vts = presentation_delay_s_.value() * timescale_;
  delay_vts = (delay_vts / vduration_ + 1) * vduration_;

  if (ts < delay_vts) return;

  uint64_t live_vts = ts - delay_vts;
  uint64_t live_ats = find_ats(live_vts);

  if (not vready(live_vts)) return;
  if (not aready(live_ats)) return;

  vlive_frontier_ = live_vts;
  alive_frontier_ = live_ats;
}

void Channel::do_mmap_video(const fs::path & filepath, const VideoFormat & vf)
{
  const mmap_t & data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    cerr << "video init: " << filepath << endl;
    vinit_.emplace(vf, data_size);
  } else {
    if (filepath.extension() == ".m4s") {
      cerr << "video chunk: " << filepath << endl;
      uint64_t ts = stoull(filestem);
      vdata_[ts][vf] = data_size;

      if (live_) {
        update_live_edge(ts);
        munmap_video(ts);

        /* assert that clean frontier < live frontier */
        if (vclean_frontier_.has_value() and vlive_frontier_.has_value()) {
          assert(vclean_frontier_.value() < vlive_frontier_.value());
        }
      }
    }
  }
}

void Channel::mmap_video_files(Inotify & inotify)
{
  for (const auto & vf : vformats_) {
    string video_dir = input_path_ / "ready" / vf.to_string();

    inotify.add_watch(video_dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        /* ignore events other than IN_MOVED_TO or moved-in directories */
        if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
          return;
        }

        assert(event.len != 0);

        fs::path filepath = fs::path(path) / event.name;
        do_mmap_video(filepath, vf);
      }
    );

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
    cerr << "audio init: " << filepath << endl;
    ainit_.emplace(af, data_size);
  } else {
    if (filepath.extension() == ".chk") {
      cerr << "audio chunk: " << filepath << endl;
      uint64_t ts = stoull(filestem);
      adata_[ts][af] = data_size;

      if (live_) {
        munmap_audio(ts);

        /* assert that clean frontier < live frontier */
        if (aclean_frontier_.has_value() and alive_frontier_.has_value()) {
          assert(aclean_frontier_.value() < alive_frontier_.value());
        }
      }
    }
  }
}

void Channel::mmap_audio_files(Inotify & inotify)
{
  for (const auto & af : aformats_) {
    string audio_dir = input_path_ / "ready" / af.to_string();

    inotify.add_watch(audio_dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        /* ignore events other than IN_MOVED_TO or moved-in directories */
        if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
          return;
        }

        assert(event.len != 0);

        fs::path filepath = fs::path(path) / event.name;
        do_mmap_audio(filepath, af);
      }
    );

    /* process existing files */
    for (const auto & file : fs::directory_iterator(audio_dir)) {
      do_mmap_audio(file.path(), af);
    }
  }
}

void Channel::do_read_ssim(const fs::path & filepath, const VideoFormat & vf) {
  if (filepath.extension() == ".ssim") {
    cerr << "ssim file: " << filepath << endl;
    string filestem = filepath.stem();
    uint64_t ts = stoull(filestem);

    std::ifstream ifs(filepath);
    std::stringstream buffer;
    buffer << ifs.rdbuf();

    vssim_[ts][vf] = stod(buffer.str());
  }
}

void Channel::load_ssim_files(Inotify & inotify)
{
  for (const auto & vf : vformats_) {
    string ssim_dir = input_path_ / "ready" / (vf.to_string() + "-ssim");

    inotify.add_watch(ssim_dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        /* ignore events other than IN_MOVED_TO or moved-in directories */
        if (not (event.mask & IN_MOVED_TO) or (event.mask & IN_ISDIR)) {
          return;
        }

        assert(event.len != 0);

        fs::path filepath = fs::path(path) / event.name;
        do_read_ssim(filepath, vf);
      }
    );

    /* process existing files */
    for (const auto & file : fs::directory_iterator(ssim_dir)) {
      do_read_ssim(file.path(), vf);
    }
  }
}
