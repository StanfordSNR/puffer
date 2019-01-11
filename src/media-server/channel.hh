#ifndef CHANNEL_HH
#define CHANNEL_HH

#include <cstdint>
#include <string>
#include <optional>
#include <map>
#include <memory>

#include "filesystem.hh"
#include "inotify.hh"
#include "mmap.hh"
#include "media_formats.hh"
#include "yaml.hh"

using mmap_t = std::tuple<std::shared_ptr<char>, size_t>;

class Channel
{
public:
  Channel(const std::string & name, const fs::path & media_dir,
          const YAML::Node & config, Inotify & inotify);

  bool live() const { return live_; }
  std::string name() const { return name_; }

  fs::path input_path() const { return input_path_; }

  const std::vector<VideoFormat> & vformats() const { return vformats_; }
  const std::vector<AudioFormat> & aformats() const { return aformats_; }

  /* if channel is ready to serve */
  bool ready_to_serve() const;
  bool vready_to_serve(const uint64_t ts) const;
  bool aready_to_serve(const uint64_t ts) const;

  /* call this function periodically (e.g., every second); mark the channel as
   * unavailable if live edge hasn't advanced for MAX_UNCHANGED_LIVE_EDGE_MS */
  void enforce_moving_live_edge();

  mmap_t vinit(const VideoFormat & format) const;
  mmap_t vdata(const VideoFormat & format, const uint64_t ts) const;
  const std::map<VideoFormat, mmap_t> & vdata(const uint64_t ts) const;
  double vssim(const VideoFormat & format, const uint64_t ts) const;
  const std::map<VideoFormat, double> & vssim(const uint64_t ts) const;

  mmap_t ainit(const AudioFormat & format) const;
  mmap_t adata(const AudioFormat & format, const uint64_t ts) const;
  const std::map<AudioFormat, mmap_t> & adata(const uint64_t ts) const;

  unsigned int timescale() const { return timescale_; }
  unsigned int vduration() const { return vduration_; }
  unsigned int aduration() const { return aduration_; }

  std::string vcodec() const { return vcodec_; }
  std::string acodec() const { return acodec_; }

  std::optional<uint64_t> init_vts() const;
  std::optional<uint64_t> init_ats() const;

  bool repeat() const { return repeat_; }

  /* return the live edge that allow for presentation_delay_s */
  std::optional<uint64_t> live_edge() const;

  /* return the frontier of contigous range of ready chunks */
  std::optional<uint64_t> vready_frontier() const { return vready_frontier_; }
  std::optional<uint64_t> aready_frontier() const { return aready_frontier_; }

  /* return largest timestamps that have been cleaned */
  std::optional<uint64_t> vclean_frontier() const;
  std::optional<uint64_t> aclean_frontier() const;

private:
  bool live_ {false};
  std::string name_ {};

  /* set by enforce_moving_live_edge */
  bool available_ {true};
  std::optional<uint64_t> last_live_edge_ {};
  std::optional<uint64_t> last_live_edge_ts_ {};

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

  /* presentation delay in unit of video chunks */
  std::optional<unsigned int> present_delay_chunk_ {};
  std::optional<uint64_t> vready_frontier_ {};
  std::optional<uint64_t> aready_frontier_ {};

  /* clean window in unit of video chunks */
  std::optional<unsigned int> clean_window_chunk_ {};
  std::optional<uint64_t> vclean_frontier_ {};
  std::optional<uint64_t> aclean_frontier_ {};

  /* configured only if live_ == false */
  std::optional<uint64_t> init_vts_ {};
  bool repeat_ {};

  bool vready(const uint64_t ts) const;
  bool aready(const uint64_t ts) const;

  uint64_t floor_vts(const uint64_t ts) const;
  uint64_t floor_ats(const uint64_t ts) const;

  bool is_valid_vts(const uint64_t ts) const { return ts % vduration_ == 0; }
  bool is_valid_ats(const uint64_t ts) const { return ts % aduration_ == 0; }

  void do_mmap_video(const fs::path & filepath, const VideoFormat & vf);
  void munmap_video(const uint64_t ts);
  void mmap_video_files(Inotify & inotify);

  void do_mmap_audio(const fs::path & filepath, const AudioFormat & af);
  void munmap_audio(const uint64_t ts);
  void mmap_audio_files(Inotify & inotify);

  void do_read_ssim(const fs::path & filepath, const VideoFormat & vf);
  void load_ssim_files(Inotify & inotify);

  void update_vready_frontier(const uint64_t vts);
  void update_aready_frontier(const uint64_t ats);
};

#endif /* CHANNEL_HH */
