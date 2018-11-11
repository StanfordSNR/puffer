#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <set>

#include "filesystem.hh"
#include "path.hh"
#include "child_process.hh"
#include "media_formats.hh"

using namespace std;

static const uint32_t global_timescale = 90000;

static fs::path src_path;
static fs::path media_dir;
static string notifier;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <YAML configuration>"
  << endl;
}

void run_video_canonicalizer(ProcessManager & proc_manager,
                             const fs::path & output_path,
                             vector<tuple<string, string>> & vwork)
{
  /* prepare directories */
  string src_dir = output_path / "working/video-raw";
  string dst_dir = output_path / "working/video-canonical";
  string tmp_dir = output_path / "tmp/video-canonical";

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  vwork.emplace_back(dst_dir, ".y4m");

  /* notifier runs video_canonicalizer */
  string video_canonicalizer = src_path / "wrappers/video_canonicalizer";

  vector<string> args {
    notifier, src_dir, ".y4m", "--check", dst_dir, ".y4m", "--tmp", tmp_dir,
    "--exec", video_canonicalizer };
  proc_manager.run_as_child(notifier, args);
}

void run_video_encoder(ProcessManager & proc_manager,
                       const fs::path & output_path,
                       vector<tuple<string, string>> & vwork,
                       const VideoFormat & vf)
{
  /* prepare directories */
  string base = vf.to_string() + "-" + "mp4";
  string src_dir = output_path / "working/video-canonical";
  string dst_dir = output_path / "working" / base;
  string tmp_dir = output_path / "tmp" / base;

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  vwork.emplace_back(dst_dir, ".mp4");

  /* notifier runs video_encoder */
  string video_encoder = src_path / "wrappers/video_encoder";

  vector<string> args {
    notifier, src_dir, ".y4m", "--check", dst_dir, ".mp4", "--tmp", tmp_dir,
    "--exec", video_encoder, "-s", vf.resolution(), "--crf", to_string(vf.crf)
  };
  proc_manager.run_as_child(notifier, args);
}

void run_video_fragmenter(ProcessManager & proc_manager,
                          const fs::path & output_path,
                          vector<tuple<string, string>> & vready,
                          const VideoFormat & vf)
{
  /* prepare directories */
  string working_base = vf.to_string() + "-" + "mp4";
  string ready_base = vf.to_string();
  string src_dir = output_path / "working" / working_base;
  string dst_dir = output_path / "ready" / ready_base;
  string tmp_dir = output_path / "tmp" / ready_base;

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  vready.emplace_back(dst_dir, ".m4s");

  /* notifier runs video_fragmenter */
  string video_fragmenter = src_path / "wrappers/video_fragmenter";
  string dst_init_path = fs::path(dst_dir) / "init.mp4";

  vector<string> args {
    notifier, src_dir, ".mp4", "--check", dst_dir, ".m4s", "--tmp", tmp_dir,
    "--exec", video_fragmenter, "-i", dst_init_path };
  proc_manager.run_as_child(notifier, args);
}

void run_ssim_calculator(ProcessManager & proc_manager,
                         const fs::path & output_path,
                         vector<tuple<string, string>> & vready,
                         const VideoFormat & vf)
{
  /* prepare directories */
  string working_base = vf.to_string() + "-" + "mp4";
  string ready_base = vf.to_string() + "-" + "ssim";
  string src_dir = output_path / "working" / working_base;
  string dst_dir = output_path / "ready" / ready_base;
  string tmp_dir = output_path / "tmp" / ready_base;
  string canonical_dir = output_path / "working/video-canonical";

  for (const auto & dir : {src_dir, dst_dir, tmp_dir, canonical_dir}) {
    fs::create_directories(dir);
  }

  vready.emplace_back(dst_dir, ".ssim");

  /* notifier runs ssim_calculator */
  string ssim_calculator = src_path / "wrappers/ssim_calculator";

  vector<string> args {
    notifier, src_dir, ".mp4", "--check", dst_dir, ".ssim", "--tmp", tmp_dir,
    "--exec", ssim_calculator, "--canonical", canonical_dir };
  proc_manager.run_as_child(notifier, args);
}

void run_audio_encoder(ProcessManager & proc_manager,
                       const fs::path & output_path,
                       vector<tuple<string, string>> & awork,
                       const AudioFormat & af)
{
  /* prepare directories */
  string base = af.to_string() + "-" + "webm";
  string src_dir = output_path / "working/audio-raw";
  string dst_dir = output_path / "working" / base;
  string tmp_dir = output_path / "tmp" / base;

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  awork.emplace_back(src_dir, ".wav");
  awork.emplace_back(dst_dir, ".webm");

  /* notifier runs audio_encoder */
  string audio_encoder = src_path / "wrappers/audio_encoder";

  vector<string> args {
    notifier, src_dir, ".wav", "--check", dst_dir, ".webm", "--tmp", tmp_dir,
    "--exec", audio_encoder, "-b", af.to_string() };
  proc_manager.run_as_child(notifier, args);
}

void run_audio_fragmenter(ProcessManager & proc_manager,
                          const fs::path & output_path,
                          vector<tuple<string, string>> & aready,
                          const AudioFormat & af)
{
  /* prepare directories */
  string working_base = af.to_string() + "-" + "webm";
  string ready_base = af.to_string();
  string src_dir = output_path / "working" / working_base;
  string dst_dir = output_path / "ready" / ready_base;
  string tmp_dir = output_path / "tmp" / ready_base;

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  aready.emplace_back(dst_dir, ".chk");

  /* notifier runs audio_fragmenter */
  string audio_fragmenter = src_path / "wrappers/audio_fragmenter";
  string dst_init_path = fs::path(dst_dir) / "init.webm";

  vector<string> args {
    notifier, src_dir, ".webm", "--check", dst_dir, ".chk", "--tmp", tmp_dir,
    "--exec", audio_fragmenter, "-i", dst_init_path };
  proc_manager.run_as_child(notifier, args);
}

void run_depcleaner(ProcessManager & proc_manager,
                    const vector<tuple<string, string>> & work,
                    const vector<tuple<string, string>> & ready)
{
  string depcleaner = src_path / "cleaner/depcleaner";
  vector<string> args = {depcleaner};

  args.emplace_back("--clean");
  for (const auto & item : work) {
    const auto & [dir, ext] = item;
    args.emplace_back(dir);
    args.emplace_back(ext);
  }

  args.emplace_back("--depend");
  for (const auto & item : ready) {
    const auto & [dir, ext] = item;
    args.emplace_back(dir);
    args.emplace_back(ext);
  }

  /* run notifier to start depcleaner for each directory in ready/ */
  for (const auto & item : ready) {
    const auto & [dir, ext] = item;
    vector<string> notifier_args { notifier, dir, ext, "--exec" };
    notifier_args.insert(notifier_args.end(), args.begin(), args.end());
    proc_manager.run_as_child(notifier, notifier_args);
  }
}

void run_windowcleaner(ProcessManager & proc_manager,
                       const vector<tuple<string, string>> & ready,
                       const int64_t clean_window_ts)
{
  string windowcleaner = src_path / "cleaner/windowcleaner";

  /* run notifier to start windowcleaner for each directory in ready/ */
  for (const auto & item : ready) {
    const auto & [dir, ext] = item;
    vector<string> notifier_args { notifier, dir, ext, "--exec", windowcleaner,
                                   ext, to_string(clean_window_ts) };
    proc_manager.run_as_child(notifier, notifier_args);
  }
}

void run_pipeline(ProcessManager & proc_manager,
                  const string & channel_name,
                  const YAML::Node & config)
{
  vector<VideoFormat> vformats = get_video_formats(config);
  vector<AudioFormat> aformats = get_audio_formats(config);

  /* tuple<directory, extension> */
  vector<tuple<string, string>> vwork, awork;
  vector<tuple<string, string>> vready, aready;

  fs::path output_path = media_dir / channel_name;
  if (fs::exists(output_path)) {
    throw runtime_error(output_path.string() + " already exists");
  }

  /* create output directory if it does not exist */
  fs::create_directories(output_path);

  /* create a tmp directory for decoder to output raw media chunks */
  fs::create_directories(output_path / "tmp" / "raw");

  /* run video_canonicalizer */
  run_video_canonicalizer(proc_manager, output_path, vwork);

  for (const auto & vf : vformats) {
    /* run video encoder and video fragmenter */
    run_video_encoder(proc_manager, output_path, vwork, vf);
    run_video_fragmenter(proc_manager, output_path, vwork, vf);

    /* run ssim_calculator */
    run_ssim_calculator(proc_manager, output_path, vready, vf);
  }

  for (const auto & af : aformats) {
    /* run audio encoder and audio fragmenter */
    run_audio_encoder(proc_manager, output_path, awork, af);
    run_audio_fragmenter(proc_manager, output_path, aready, af);
  }

  /* vwork, awork, vready, aready should already be filled in */

  /* run depcleaner to clean up files in working/ */
  run_depcleaner(proc_manager, vwork, vready);
  run_depcleaner(proc_manager, awork, aready);

  /* run windowcleaner to clean up files in ready/ */
  int64_t clean_window_ts = config["clean_window_s"].as<int>() * global_timescale;
  run_windowcleaner(proc_manager, vready, clean_window_ts);
  run_windowcleaner(proc_manager, aready, clean_window_ts);
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* load YAML configuration */
  YAML::Node config = YAML::LoadFile(argv[1]);

  /* get the path of wrappers directory and notifier */
  src_path = fs::canonical(fs::path(
             roost::readlink("/proc/self/exe")).parent_path().parent_path());
  notifier = src_path / "notifier/notifier";
  media_dir = config["media_dir"].as<string>();

  ProcessManager proc_manager;

  /* temporary set of channels to check if duplicate channels exist */
  set<string> channel_set;
  for (YAML::const_iterator it = config["channels"].begin();
       it != config["channels"].end(); ++it) {
    const string & channel_name = it->as<string>();

    if (not config["channel_configs"][channel_name]) {
      throw runtime_error("Cannot find details of channel: " + channel_name);
    }

    if (not channel_set.insert(channel_name).second) {
      throw runtime_error("Found duplicate channel: " + channel_name);
    }

    /* run the encoding pipeline for channel_name */
    run_pipeline(proc_manager,
                 channel_name, config["channel_configs"][channel_name]);
  }

  return proc_manager.wait();
}
