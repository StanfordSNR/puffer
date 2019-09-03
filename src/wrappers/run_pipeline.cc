#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <set>
#include <getopt.h>

#include "filesystem.hh"
#include "path.hh"
#include "child_process.hh"
#include "media_formats.hh"
#include "tokenize.hh"
#include "yaml.hh"

using namespace std;

static const uint32_t global_timescale = 90000;
static const uint32_t clean_window_s = 60;

static fs::path src_path;
static fs::path media_dir;
static string notifier;

static bool no_decoder = false;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <YAML configuration> [--no-decoder]"
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
  string audio_encoder = src_path / "opus-encoder/opus-encoder";

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

void run_file_sender(ProcessManager & proc_manager,
                     const vector<tuple<string, string>> & ready,
                     const YAML::Node & config)
{
  string host = config["host"].as<string>();
  uint16_t port = config["port"].as<uint16_t>();
  fs::path dst_media_dir = config["media_dir"].as<string>();
  string file_sender = src_path / "forwarder/file_sender";

  for (const auto & item : ready) {
    const auto & dir = std::get<0>(item);

    /* remove the prefix of media_dir from dir and append to dst_media_dir */
    string remaining = dir.substr(media_dir.string().size());
    string dst_dir = dst_media_dir / remaining;

    /* file sender is interested in any move-in files
     * e.g., init.mp4 and .m4s in a vready dir */
    vector<string> notifier_args { notifier, dir, ".", "--exec",
      file_sender, host, to_string(port), dst_dir };
    proc_manager.run_as_child(notifier, notifier_args);
  }
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
                       const unsigned int clean_window_ts)
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

void run_decoder(ProcessManager & proc_manager,
                 const fs::path & output_path,
                 const string & channel_name,
                 const YAML::Node & config)
{
  /* prepare directories */
  string video_raw = output_path / "working/video-raw";
  string audio_raw = output_path / "working/audio-raw";
  string tmp_raw = output_path / "tmp/raw";

  for (const auto & dir : {video_raw, audio_raw, tmp_raw}) {
    fs::create_directories(dir);
  }

  string decoder = src_path / "atsc/decoder";
  vector<string> decoder_args = split(config["decoder_args"].as<string>(), " ");
  string decoder_log = src_path / "atsc" / (channel_name + "_decoder.log");

  vector<string> args { decoder, video_raw, audio_raw, "--tmp", tmp_raw };
  args.insert(args.begin() + 1, decoder_args.begin(), decoder_args.end());

  proc_manager.run_as_child(decoder, args, {}, {}, decoder_log);
}

void run_pipeline(ProcessManager & proc_manager,
                  const string & channel_name,
                  const YAML::Node & config)
{
  const auto & channel_config = config["channel_configs"][channel_name];
  vector<VideoFormat> vformats = channel_video_formats(channel_config);
  vector<AudioFormat> aformats = channel_audio_formats(channel_config);

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
    run_video_fragmenter(proc_manager, output_path, vready, vf);

    /* run ssim_calculator */
    run_ssim_calculator(proc_manager, output_path, vready, vf);
  }

  for (const auto & af : aformats) {
    /* run audio encoder and audio fragmenter */
    run_audio_encoder(proc_manager, output_path, awork, af);
    run_audio_fragmenter(proc_manager, output_path, aready, af);
  }

  if (config["remote_media_server"]) {
    /* run file_sender to transfer files in ready/ */
    run_file_sender(proc_manager, vready, config["remote_media_server"]);
    run_file_sender(proc_manager, aready, config["remote_media_server"]);
  }

  /* vwork, awork, vready, aready should already be filled in */

  /* run depcleaner to clean up files in working/ */
  run_depcleaner(proc_manager, vwork, vready);
  run_depcleaner(proc_manager, awork, aready);

  /* run windowcleaner to clean up files in ready/ */
  unsigned int clean_window_ts = clean_window_s * global_timescale;
  run_windowcleaner(proc_manager, vready, clean_window_ts);
  run_windowcleaner(proc_manager, aready, clean_window_ts);

  /* run decoder */
  if (not no_decoder) {
    run_decoder(proc_manager, output_path, channel_name, channel_config);
  }
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  const option cmd_line_opts[] = {
    {"no-decoder", no_argument, nullptr, 'd'},
    { nullptr,     0,           nullptr,  0 },
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "d", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'd':
      no_decoder = true;
      break;
    default:
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind != argc - 1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* load YAML configuration */
  string yaml_config = fs::absolute(argv[optind]);
  YAML::Node config = YAML::LoadFile(yaml_config);

  /* get the path of wrappers directory and notifier */
  src_path = fs::canonical(fs::path(
             roost::readlink("/proc/self/exe")).parent_path().parent_path());
  notifier = src_path / "notifier/notifier";
  media_dir = config["media_dir"].as<string>();

  ProcessManager proc_manager;

  set<string> channel_set = load_channels(config);
  for (const auto & channel_name : channel_set) {
    /* run the encoding pipeline for channel_name */
    run_pipeline(proc_manager, channel_name, config);
  }

  /* if logging is enabled */
  if (config["enable_logging"].as<bool>()) {
    fs::path monitoring_dir = src_path / "monitoring";

    /* report SSIMs, video chunk sizes, backlog sizes and .y4m.info files */
    string file_reporter = monitoring_dir / "file_reporter";
    vector<string> file_reporter_args { file_reporter, yaml_config };
    proc_manager.run_as_child(file_reporter, file_reporter_args);
  }

  return proc_manager.wait();
}
