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

static string run_notifier;
static fs::path output_path;
static fs::path wrappers_path;

static vector<tuple<string, string>> vformats;
static vector<string> aformats;

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

void run_canonicalizer(ProcessManager & proc_manager)
{
  /* prepare directories */
  string src_dir = output_path / "working/video-raw";
  string dst_dir = output_path / "working/video-canonical";
  string tmp_dir = output_path / "tmp/video-canonical";

  for (const auto & dir : {src_dir, dst_dir, tmp_dir}) {
    fs::create_directories(dir);
  }

  /* run_notifier calls canonicalizer */
  string canonicalizer = wrappers_path / "canonicalizer";

  vector<string> args {
    run_notifier, src_dir, "--check", dst_dir, canonicalizer,
    dst_dir, "--tmp", tmp_dir };
  proc_manager.run_as_child(run_notifier, args);
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
  /* create output_dir if it does not already exist */
  if (fs::exists(output_dir)) {
    throw runtime_error(output_dir + " already exists");
  }
  output_path = fs::path(output_dir);

  /* get the path of wrappers directory and run_notifier */
  wrappers_path = fs::canonical(fs::path(
                      roost::readlink("/proc/self/exe")).parent_path());
  run_notifier = fs::canonical(wrappers_path / "../notifier/run_notifier");

  ProcessManager proc_manager;

  /* run canonicalizer */
  run_canonicalizer(proc_manager);

  proc_manager.wait();

  return EXIT_SUCCESS;
}
