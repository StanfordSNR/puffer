#include "channel.hh"

#include <limits.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>

#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

Channel::Channel(const string & name, YAML::Node config, Inotify & inotify)
{
  name_ = name;

  string output_dir = config["output"].as<string>();
  output_path_ = fs::path(output_dir);

  vformats_ = get_video_formats(config);
  aformats_ = get_audio_formats(config);

  vcodec_ = config["video_codec"].as<string>();
  acodec_ = config["audio_codec"].as<string>();

  if (config["clean_time_window"]) {
    clean_time_window_ = config["clean_time_window"].as<uint64_t>();
  }
  timescale_ = config["timescale"].as<unsigned int>();
  vduration_ = config["video_duration"].as<unsigned int>();
  aduration_ = config["audio_duration"].as<unsigned int>();

  if (config["init_vts"]) {
    init_vts_ = config["init_vts"].as<uint64_t>();
    assert(init_vts_.value() % vduration_ == 0);
  }

  mmap_video_files(inotify);
  mmap_audio_files(inotify);
  load_ssim_files(inotify);
}

optional<uint64_t> Channel::vready_frontier(const unsigned int n) const
{
  unsigned int tmp = n;
  for (auto it = vdata_.rbegin(); it != vdata_.rend(); ++it) {
    uint64_t ts = it->first;
    if (vready(ts)) {
      if (tmp == 0) {
        return ts;
      } else {
        tmp--;
      }
    }
  }
  return nullopt;
}

optional<uint64_t> Channel::aready_frontier(const unsigned int n) const
{
  unsigned int tmp = n;
  for (auto it = adata_.rbegin(); it != adata_.rend(); ++it) {
    uint64_t ts = it->first;
    if (aready(ts)) {
      if (tmp == 0) {
        return ts;
      } else {
        tmp--;
      }
    }
  }
  return nullopt;
}

optional<uint64_t> Channel::init_vts(const unsigned int max_playback_buf) const
{
  if (init_vts_.has_value()) {
    return init_vts_.value(); /* The user configured a fixed VTS */
  } else {
    /* Choose the newest vts with all qualities available that allows for
     * a maximum playback buffer */
    return vready_frontier(max_playback_buf * timescale_ / vduration_ + 1);
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
  if (it2 == vssim_.end() or it1->second.size() != vformats_.size()) {
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
  assert(is_valid_vts(ts));
  return vdata_.at(ts).at(format);
}

map<VideoFormat, mmap_t> & Channel::vdata(const uint64_t ts)
{
  assert(is_valid_vts(ts));
  return vdata_.at(ts);
}

double Channel::vssim(const VideoFormat & format, const uint64_t ts)
{
  assert(is_valid_vts(ts));
  return vssim_.at(ts).at(format);
}

map<VideoFormat, double> & Channel::vssim(const uint64_t ts)
{
  assert(is_valid_vts(ts));
  return vssim_.at(ts);
}

bool Channel::aready(const uint64_t ts) const
{
  assert(is_valid_ats(ts));
  auto it = adata_.find(ts);
  return ainit_.size() == aformats_.size() and
      it != adata_.end() and it->second.size() == aformats_.size();
}

mmap_t & Channel::ainit(const AudioFormat & format)
{
  return ainit_.at(format);
}

mmap_t & Channel::adata(const AudioFormat & format, const uint64_t ts)
{
  assert(is_valid_ats(ts));
  return adata_.at(ts).at(format);
}

static mmap_t mmap_file(const string & filepath)
{
  FileDescriptor fd(CheckSystemCall("open (" + filepath + ")",
                    open(filepath.c_str(), O_RDONLY)));
  size_t size = fd.filesize();
  shared_ptr<void> data = mmap_shared(nullptr, size, PROT_READ,
                                      MAP_PRIVATE, fd.fd_num(), 0);
  return {static_pointer_cast<char>(data), size};
}

void Channel::munmap_video(const uint64_t latest_ts)
{
  if (clean_time_window_.has_value() and
      latest_ts >= clean_time_window_.value()) {
    uint64_t obsolete = latest_ts - clean_time_window_.value();

    for (auto it = vdata_.cbegin(); it != vdata_.cend();) {
      uint64_t ts = it->first;
      if (ts <= obsolete) {
        vssim_.erase(ts);
        it = vdata_.erase(it);
      } else {
        break;
      }
    }

    if (not vclean_frontier_.has_value() or
        vclean_frontier_.value() < obsolete) {
      vclean_frontier_ = obsolete;
    }
  }
}

void Channel::munmap_audio(const uint64_t latest_ts)
{
  if (clean_time_window_.has_value() and
      latest_ts >= clean_time_window_.value()) {
    uint64_t obsolete = latest_ts - clean_time_window_.value();

    for (auto it = adata_.cbegin(); it != adata_.cend();) {
      if (it->first <= obsolete) {
        it = adata_.erase(it);
      } else {
        break;
      }
    }

    if (not aclean_frontier_.has_value() or
        aclean_frontier_.value() < obsolete) {
      aclean_frontier_ = obsolete;
    }
  }
}

void Channel::do_mmap_video(const fs::path & filepath, const VideoFormat & vf)
{
  auto data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    cerr << "video init: " << filepath << endl;
    vinit_.emplace(vf, data_size);
  } else {
    if (filepath.extension() == ".m4s") {
      cerr << "video file: " << filepath << endl;
      uint64_t ts = stoll(filestem);
      munmap_video(ts);
      vdata_[ts][vf] = data_size;
    }
  }
}

void Channel::mmap_video_files(Inotify & inotify)
{
  for (const auto & vf : vformats_) {
    string video_dir = output_path_ / "ready" / vf.to_string();
    cerr << "video dir: " << video_dir << endl;

    inotify.add_watch(video_dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        if (not (event.mask & IN_MOVED_TO)) {
          /* only interested in event IN_MOVED_TO */
          return;
        }

        if (event.mask & IN_ISDIR) {
          /* ignore directories moved into source directory */
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
  auto data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    cerr << "audio init: " << filepath << endl;
    ainit_.emplace(af, data_size);
  } else {
    if (filepath.extension() == ".chk") {
      cerr << "audio chunk: " << filepath << endl;
      uint64_t ts = stoll(filestem);
      munmap_audio(ts);
      adata_[ts][af] = data_size;
    }
  }
}

void Channel::mmap_audio_files(Inotify & inotify)
{
  for (const auto & af : aformats_) {
    string audio_dir = output_path_ / "ready" / af.to_string();
    cerr << "audio dir: " << audio_dir << endl;

    inotify.add_watch(audio_dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        if (not (event.mask & IN_MOVED_TO)) {
          /* only interested in event IN_MOVED_TO */
          return;
        }

        if (event.mask & IN_ISDIR) {
          /* ignore directories moved into source directory */
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
    uint64_t ts = stoll(filestem);

    std::ifstream ifs(filepath);
    std::stringstream buffer;
    buffer << ifs.rdbuf();

    vssim_[ts][vf] = stod(buffer.str());
  }
}

void Channel::load_ssim_files(Inotify & inotify)
{
  for (const auto & vf : vformats_) {
    string ssim_dir = output_path_ / "ready" / (vf.to_string() + "-ssim");
    cerr << "ssim dir: " << ssim_dir << endl;

    inotify.add_watch(ssim_dir, IN_MOVED_TO,
      [&](const inotify_event & event, const string & path) {
        if (not (event.mask & IN_MOVED_TO)) {
          /* only interested in event IN_MOVED_TO */
          return;
        }

        if (event.mask & IN_ISDIR) {
          /* ignore directories moved into source directory */
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
