#include "channel.hh"

#include <fcntl.h>

#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

Channel::Channel(const string & name, YAML::Node config, Inotify & inotify)
: name_(name),
  output_path_(),
  vformats_(), aformats_(),
  vinit_(), ainit_(),
  vdata_(), adata_(),
  clean_time_window_(),
  timescale_(),
  vduration_(), aduration_(),
  vcodec_(), acodec_(),
  init_vts_()
{
  vformats_ = get_video_formats(config);
  aformats_ = get_audio_formats(config);

  string output_dir = config["output"].as<string>();
  output_path_ = fs::path(output_dir);

  vcodec_ = config["video_codec"].as<string>();
  acodec_ = config["audio_codec"].as<string>();

  clean_time_window_ = config["clean_time_window"].as<int>();
  timescale_ = config["timescale"].as<int>();
  vduration_ = config["video_duration"].as<int>();
  aduration_ = config["audio_duration"].as<int>();

  mmap_video_files(inotify);
  mmap_audio_files(inotify);
}

uint64_t Channel::init_vts() const
{
  if (init_vts_.has_value()) {
    return init_vts_.value(); /* The user configured a fixed VTS */
  } else {
    /* Choose the newest vts with all qualities available */
    for (auto it = vdata_.rbegin(); it != vdata_.rend(); ++it) {
      if (it->second.size() == vformats_.size()) {
        return it->first;
      }
    }
    cerr << "Encoder is in a bad state, no vts has all qualities available" << endl;
    abort();
  }
}

uint64_t Channel::find_ats(const uint64_t vts) const
{
  return (vts / aduration_) * aduration_;
}

mmap_t & Channel::vinit(const VideoFormat & format)
{
  return vinit_.at(format);
}

mmap_t & Channel::vdata(const VideoFormat & format, const uint64_t ts)
{
  return vdata_.at(ts).at(format);
}

mmap_t & Channel::ainit(const AudioFormat & format)
{
  return ainit_.at(format);
}

mmap_t & Channel::adata(const AudioFormat & format, const uint64_t ts)
{
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

void Channel::munmap_video(const uint64_t ts)
{
  uint64_t obsolete = 0;
  if (ts > clean_time_window_) {
    obsolete = ts - clean_time_window_;
  }

  for (auto it = vdata_.cbegin(); it != vdata_.cend();) {
    if (it->first < obsolete) {
      it = vdata_.erase(it);
    } else {
      break;
    }
  }
}

void Channel::munmap_audio(const uint64_t ts)
{
  uint64_t obsolete = 0;
  if (ts > clean_time_window_) {
    obsolete = ts - clean_time_window_;
  }

  for (auto it = adata_.cbegin(); it != adata_.cend();) {
    if (it->first < obsolete) {
      it = adata_.erase(it);
    } else {
      break;
    }
  }
}

void Channel::do_mmap_video(const fs::path & filepath, const VideoFormat & vf)
{
  auto data_size = mmap_file(filepath);
  string filestem = filepath.stem();

  if (filestem == "init") {
    vinit_.emplace(vf, data_size);
  } else {
    uint64_t ts = stoll(filestem);
    munmap_video(ts);
    vdata_[ts][vf] = data_size;
  }
}

void Channel::mmap_video_files(Inotify & inotify)
{
  for (const auto & vf : vformats_) {
    string video_dir = output_path_ / "ready" / vf.to_string();

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
    ainit_.emplace(af, data_size);
  } else {
    uint64_t ts = stoll(filestem);
    munmap_audio(ts);
    adata_[ts][af] = data_size;
  }
}

void Channel::mmap_audio_files(Inotify & inotify)
{
  for (const auto & af : aformats_) {
    string audio_dir = output_path_ / "ready" / af.to_string();

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