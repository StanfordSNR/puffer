#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>

#include "filesystem.hh"
#include "exception.hh"
#include "path.hh"
#include "child_process.hh"
#include "yaml-cpp/yaml.h"

using namespace std;

static string notifier;
static fs::path output_path;
static fs::path wrappers_path;
static fs::path clean_path;

static vector<tuple<string, string>> vformats;
static vector<string> aformats;

static vector<tuple<string, string>> working_dir_ext;
static vector<tuple<string, string>> ready_dir_ext;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <YAML configuration>"
  << endl;
}

/* load and validate the YAML file */
YAML::Node load_yaml(const string & yaml_path)
{
  YAML::Node config = YAML::LoadFile(yaml_path);

  if (not config["input"]) {
    throw runtime_error("invalid YAML: input is not present");
  }

  if (not config["output"]) {
    throw runtime_error("invalid YAML: output is not present");
  }

  if (not config["video"]) {
    throw runtime_error("invalid YAML: video is not present");
  }

  if (not config["audio"]) {
    throw runtime_error("invalid YAML: audio is not present");
  }

  return config;
}

/* get video formats (resolution, CRF) from YAML configuration */
void get_video_formats(const YAML::Node & config)
{
  const YAML::Node & res_map = config["video"];
  for (const auto & res_node : res_map) {
    const string & res = res_node.first.as<string>();

    const YAML::Node & crf_list = res_node.second;
    for (const auto & crf_node : crf_list) {
      const string & crf = crf_node.as<string>();
      vformats.emplace_back(res, crf);
    }
  }
}

/* get audio formats (bitrate) from YAML configuration */
void get_audio_formats(const YAML::Node & config)
{
  const YAML::Node & bitrate_list = config["audio"];
  for (const auto & bitrate_node : bitrate_list) {
    const string & bitrate = bitrate_node.as<string>();
    aformats.emplace_back(bitrate);
  }
}

void run_video_canonicalizer(ProcessManager & proc_manager)
{
  /* prepare directories */
  string src_dir = output_path / "working/video-raw";
  string dst_dir = output_path / "working/video-canonical";
  string tmp_dir = output_path / "tmp/video-canonical";

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  working_dir_ext.emplace_back(dst_dir, ".y4m");

  /* notifier runs video_canonicalizer */
  string video_canonicalizer = wrappers_path / "video_canonicalizer";

  vector<string> args {
    notifier, src_dir, ".y4m", "--check", dst_dir, ".y4m", "--tmp", tmp_dir,
    "--exec", video_canonicalizer };
  proc_manager.run_as_child(notifier, args);
}

void run_video_encoder(ProcessManager & proc_manager,
                       const tuple<string, string> & vformat)
{
  const auto & [res, crf] = vformat;

  /* prepare directories */
  string base = res + "-" + crf + "-" + "mp4";
  string src_dir = output_path / "working/video-canonical";
  string dst_dir = output_path / "working" / base;
  string tmp_dir = output_path / "tmp" / base;

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  working_dir_ext.emplace_back(dst_dir, ".mp4");

  /* notifier runs video_encoder */
  string video_encoder = wrappers_path / "video_encoder";

  vector<string> args {
    notifier, src_dir, ".y4m", "--check", dst_dir, ".mp4", "--tmp", tmp_dir,
    "--exec", video_encoder, "-s", res, "--crf", crf };
  proc_manager.run_as_child(notifier, args);
}

void run_video_fragmenter(ProcessManager & proc_manager,
                          const tuple<string, string> & vformat)
{
  const auto & [res, crf] = vformat;

  /* prepare directories */
  string working_base = res + "-" + crf + "-" + "mp4";
  string ready_base = res + "-" + crf;
  string src_dir = output_path / "working" / working_base;
  string dst_dir = output_path / "ready" / ready_base;
  string tmp_dir = output_path / "tmp" / ready_base;

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  ready_dir_ext.emplace_back(dst_dir, ".m4s");

  /* notifier runs video_fragmenter */
  string video_fragmenter = wrappers_path / "video_fragmenter";
  string dst_init_path = fs::path(dst_dir) / "init.mp4";

  vector<string> args {
    notifier, src_dir, ".mp4", "--check", dst_dir, ".m4s", "--tmp", tmp_dir,
    "--exec", video_fragmenter, "-i", dst_init_path };
  proc_manager.run_as_child(notifier, args);
}

void run_ssim_calculator(ProcessManager & proc_manager,
                         const tuple<string, string> & vformat)
{
  const auto & [res, crf] = vformat;

  /* prepare directories */
  string working_base = res + "-" + crf + "-" + "mp4";
  string ready_base = res + "-" + crf + "-" + "ssim";
  string src_dir = output_path / "working" / working_base;
  string dst_dir = output_path / "ready" / ready_base;
  string tmp_dir = output_path / "tmp" / ready_base;
  string canonical_dir = output_path / "working/video-canonical";

  for (const auto & dir : {src_dir, dst_dir, tmp_dir, canonical_dir}) {
    fs::create_directories(dir);
  }

  ready_dir_ext.emplace_back(dst_dir, ".ssim");

  /* notifier runs ssim_calculator */
  string ssim_calculator = wrappers_path / "ssim_calculator";

  vector<string> args {
    notifier, src_dir, ".mp4", "--check", dst_dir, ".ssim", "--tmp", tmp_dir,
    "--exec", ssim_calculator, "--canonical", canonical_dir };
  proc_manager.run_as_child(notifier, args);
}

void run_audio_encoder(ProcessManager & proc_manager,
                       const string & bitrate)
{
  /* prepare directories */
  string base = bitrate + "-" + "webm";
  string src_dir = output_path / "working/audio-raw";
  string dst_dir = output_path / "working" / base;
  string tmp_dir = output_path / "tmp" / base;

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  working_dir_ext.emplace_back(src_dir, ".wav");
  working_dir_ext.emplace_back(dst_dir, ".webm");

  /* notifier runs audio_encoder */
  string audio_encoder = wrappers_path / "audio_encoder";

  vector<string> args {
    notifier, src_dir, ".wav", "--check", dst_dir, ".webm", "--tmp", tmp_dir,
    "--exec", audio_encoder, "-b", bitrate };
  proc_manager.run_as_child(notifier, args);
}

void run_audio_fragmenter(ProcessManager & proc_manager,
                          const string & bitrate)
{
  /* prepare directories */
  string working_base = bitrate + "-" + "webm";
  string ready_base = bitrate;
  string src_dir = output_path / "working" / working_base;
  string dst_dir = output_path / "ready" / ready_base;
  string tmp_dir = output_path / "tmp" / ready_base;

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  ready_dir_ext.emplace_back(dst_dir, ".chk");

  /* notifier runs audio_fragmenter */
  string audio_fragmenter = wrappers_path / "audio_fragmenter";
  string dst_init_path = fs::path(dst_dir) / "init.webm";

  vector<string> args {
    notifier, src_dir, ".webm", "--check", dst_dir, ".chk", "--tmp", tmp_dir,
    "--exec", audio_fragmenter, "-i", dst_init_path };
  proc_manager.run_as_child(notifier, args);
}

void run_depcleaner(ProcessManager & proc_manager)
{
  string depcleaner = clean_path / "depcleaner";
  vector<string> args = {depcleaner};

  args.emplace_back("--clean");
  for (const auto & item : working_dir_ext) {
    const auto & [dir, ext] = item;
    args.emplace_back(dir);
    args.emplace_back(ext);
  }

  args.emplace_back("--depend");
  for (const auto & item : ready_dir_ext) {
    const auto & [dir, ext] = item;
    args.emplace_back(dir);
    args.emplace_back(ext);
  }

  /* run notifier for each directory in ready/ */
  for (const auto & item : ready_dir_ext) {
    const auto & [dir, ext] = item;
    vector<string> notifier_args { notifier, dir, ext, "--exec" };
    notifier_args.insert(notifier_args.end(), args.begin(), args.end());
    proc_manager.run_as_child(notifier, notifier_args);
  }
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

  /* load and validate YAML that contains arguments */
  YAML::Node config = load_yaml(argv[1]);
  get_video_formats(config);
  get_audio_formats(config);

  string output_dir = config["output"].as<string>();

  if (fs::exists(output_dir)) {
    /* clean up output_dir if overwrite_output is true */
    if (config["overwrite_output"] and config["overwrite_output"].as<bool>()) {
      fs::remove_all(output_dir);
    } else {
      cerr << output_dir + " already exists" << endl;
      return EXIT_FAILURE;
    }
  }

  output_path = fs::path(output_dir);
  fs::create_directory(output_path);

  /* get the path of wrappers directory and notifier */
  wrappers_path = fs::canonical(fs::path(
                      roost::readlink("/proc/self/exe")).parent_path());
  clean_path = fs::canonical(wrappers_path / "../cleaner");
  notifier = fs::canonical(wrappers_path / "../notifier/notifier");

  ProcessManager proc_manager;

  /* run video_canonicalizer */
  run_video_canonicalizer(proc_manager);

  for (const auto & vformat : vformats) {
    /* run video encoder and video fragmenter */
    run_video_encoder(proc_manager, vformat);
    run_video_fragmenter(proc_manager, vformat);

    /* run ssim_calculator */
    run_ssim_calculator(proc_manager, vformat);
  }

  for (const auto & aformat : aformats) {
    /* run audio encoder and audio fragmenter */
    run_audio_encoder(proc_manager, aformat);
    run_audio_fragmenter(proc_manager, aformat);
  }

  /* run depcleaner to clean up files in working/ */
  run_depcleaner(proc_manager);

  return proc_manager.wait();
}
