#ifndef CHANNEL_HH
#define CHANNEL_HH

#include <cstdint>
#include <string>
#include <optional>
#include <map>
#include <memory>

#include "yaml.hh"
#include "filesystem.hh"
#include "inotify.hh"
#include "mmap.hh"

using mmap_t = std::tuple<std::shared_ptr<char>, size_t>;

class Channel
{
public:
  Channel(const std::string & name, YAML::Node config, Inotify & inotify);

  bool live() const { return live_; }
  const std::string & name() const { return name_; }

  fs::path input_path() const { return input_path_; }

  const std::vector<VideoFormat> & vformats() const { return vformats_; }
  const std::vector<AudioFormat> & aformats() const { return aformats_; }

  bool vready(const uint64_t ts) const;
  mmap_t & vinit(const VideoFormat & format);
  mmap_t & vdata(const VideoFormat & format, const uint64_t ts);
  std::map<VideoFormat, mmap_t> & vdata(const uint64_t ts);
  double vssim(const VideoFormat & format, const uint64_t ts);
  std::map<VideoFormat, double> & vssim(const uint64_t ts);

  bool aready(const uint64_t ts) const;
  mmap_t & ainit(const AudioFormat & format);
  mmap_t & adata(const AudioFormat & format, const uint64_t ts);

  unsigned int timescale() const { return timescale_; }
  unsigned int vduration() const { return vduration_; }
  unsigned int aduration() const { return aduration_; }

  const std::string & vcodec() const { return vcodec_; }
  const std::string & acodec() const { return acodec_; }

  std::optional<uint64_t> init_vts() const;
  uint64_t find_ats(const uint64_t vts) const;

  bool is_valid_vts(const uint64_t ts) const { return ts % vduration_ == 0; }
  bool is_valid_ats(const uint64_t ts) const { return ts % aduration_ == 0; }

  /* return live edges that allow for presentation_delay_s */
  std::optional<uint64_t> vlive_frontier() const { return vlive_frontier_; }
  std::optional<uint64_t> alive_frontier() const { return alive_frontier_; }

  /* return largest timestamps that have been cleaned */
  std::optional<uint64_t> vclean_frontier() const { return vclean_frontier_; }
  std::optional<uint64_t> aclean_frontier() const { return aclean_frontier_; }

private:
  bool live_ {false};
  std::string name_ {};

  fs::path input_path_ {};
  std::vector<VideoFormat> vformats_ {};
  std::vector<AudioFormat> aformats_ {};
  std::map<VideoFormat, mmap_t> vinit_ {};
  std::map<AudioFormat, mmap_t> ainit_ {};
  std::map<uint64_t, std::map<VideoFormat, mmap_t>> vdata_ {};
  std::map<uint64_t, std::map<VideoFormat, double>> vssim_ {};
  std::map<uint64_t, std::map<AudioFormat, mmap_t>> adata_ {};

  unsigned int timescale_ {};
  unsigned int vduration_ {};
  unsigned int aduration_ {};
  std::string vcodec_ {};
  std::string acodec_ {};

  /* live_ == true */
  std::optional<unsigned int> presentation_delay_s_ {};
  std::optional<uint64_t> vlive_frontier_ {};
  std::optional<uint64_t> alive_frontier_ {};

  std::optional<unsigned int> clean_window_s_ {};
  std::optional<uint64_t> vclean_frontier_ {};
  std::optional<uint64_t> aclean_frontier_ {};

  /* configured only if live_ == false */
  std::optional<uint64_t> init_vts_ {};

  void do_mmap_video(const fs::path & filepath, const VideoFormat & vf);
  void munmap_video(const uint64_t ts);
  void mmap_video_files(Inotify & inotify);

  void do_mmap_audio(const fs::path & filepath, const AudioFormat & af);
  void munmap_audio(const uint64_t ts);
  void mmap_audio_files(Inotify & inotify);

  void do_read_ssim(const fs::path & filepath, const VideoFormat & vf);
  void load_ssim_files(Inotify & inotify);

  void update_live_edge(const uint64_t ts);
};

#endif /* CHANNEL_HH */
