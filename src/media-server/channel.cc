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
  live_ = config["live"].as<bool>();
  name_ = name;

  string input_dir = config["input"].as<string>();
  input_path_ = fs::path(input_dir);

  vformats_ = get_video_formats(config);
  aformats_ = get_audio_formats(config);

  vcodec_ = config["video_codec"].as<string>();
  acodec_ = config["audio_codec"].as<string>();

  timescale_ = config["timescale"].as<unsigned int>();
  vduration_ = config["video_duration"].as<unsigned int>();
  aduration_ = config["audio_duration"].as<unsigned int>();

  if (live_) {
    presentation_delay_s_ = config["presentation_delay_s"].as<unsigned int>();
    clean_window_s_ = config["clean_window_s"].as<unsigned int>();

    /* ensure an enough gap between clean_window_s and presentation_delay_s_ */
    assert(presentation_delay_s_.value() + 10 < clean_window_s_.value());
  } else {
    init_vts_ = config["init_vts"].as<uint64_t>();
    assert(init_vts_.value() % vduration_ == 0);
  }

  mmap_video_files(inotify);
  mmap_audio_files(inotify);
  load_ssim_files(inotify);
}

uint64_t Channel::init_vts() const
{
  if (init_vts_.has_value()) {
    /* the user configured a fixed VTS */
    return init_vts_.value();
  } else {
    /* choose the newest vts with all qualities available that allows for
     * at least presentation_delay_s */
    uint64_t newest_vts = vdata_.cend()->first;
    unsigned int delay_vts = presentation_delay_s_.value() * timescale_;
    assert(newest_vts >= delay_vts);

    int n = (newest_vts - delay_vts) / vduration_ + 1;
    assert(newest_vts >= n * vduration_);

    uint64_t init_vts = newest_vts - n * vduration_;
    assert(init_vts >= vdata_.cbegin()->first);

    return init_vts;
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
  if (clean_window_s_.has_value()) {
    uint64_t clean_window_ts = clean_window_s_.value() * timescale_;
    if (latest_ts < clean_window_ts) {
      return;
    }
    uint64_t obsolete = latest_ts - clean_window_ts;

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
    if (not cleaned_ts.has_value()) {
      return;
    }

    if (not vclean_frontier_.has_value() or
        vclean_frontier_.value() < cleaned_ts.value()) {
      vclean_frontier_ = cleaned_ts.value();
    }
  }
}

void Channel::munmap_audio(const uint64_t latest_ts)
{
  if (clean_window_s_.has_value()) {
    uint64_t clean_window_ts = clean_window_s_.value() * timescale_;
    if (latest_ts < clean_window_ts) {
      return;
    }
    uint64_t obsolete = latest_ts - clean_window_ts;

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
    if (not cleaned_ts.has_value()) {
      return;
    }

    if (not aclean_frontier_.has_value() or
        aclean_frontier_.value() < cleaned_ts) {
      aclean_frontier_ = cleaned_ts.value();
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
      vdata_[ts][vf] = data_size;

      if (live_) {
        munmap_video(ts);
      }
    }
  }
}

void Channel::mmap_video_files(Inotify & inotify)
{
  for (const auto & vf : vformats_) {
    string video_dir = input_path_ / "ready" / vf.to_string();
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
      adata_[ts][af] = data_size;

      if (live_) {
        munmap_audio(ts);
      }
    }
  }
}

void Channel::mmap_audio_files(Inotify & inotify)
{
  for (const auto & af : aformats_) {
    string audio_dir = input_path_ / "ready" / af.to_string();
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
    string ssim_dir = input_path_ / "ready" / (vf.to_string() + "-ssim");
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
