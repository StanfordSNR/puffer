#include <getopt.h>
#include <time.h>
#include <iostream>
#include <string>
#include <chrono>
#include <memory>
#include <set>
#include <algorithm>

#include "mpd.hh"
#include "path.hh"
#include "mp4_info.hh"
#include "mp4_parser.hh"
#include "tokenize.hh"
#include "file_descriptor.hh"
#include "exception.hh"
#include "webm_info.hh"

using namespace std;
using namespace MPD;
using namespace MP4;

const uint32_t global_timescale = 90000;

const char default_base_uri[] = "/";
const char default_audio_uri[] = "$RepresentationID$/$Time$.chk";
const char default_video_uri[] = "$RepresentationID$/$Time$.m4s";
const char default_video_init_uri[] = "$RepresentationID$/init.mp4";
const char default_audio_init_uri[] = "$RepresentationID$/init.webm";
const uint32_t default_buffer_time = 2;
const string default_time_uri = "/time";

const set<string> media_extension {"m4s", "chk", "webm"};

void print_usage(const string & program_name)
{
  cerr
  << "Usage: " << program_name << " [options] <dir> <dir> ...\n\n"
  "<dir>                        Path to video/audio folders. If media segment\n"
  "                             does not exists in the folder, program will\n"
  "                             exist without outputing mpd.\n"
  "-u --url <base_url>          Set the base url for all media segments.\n"
  "-b --buffer-time <time>      Set the minimum buffer time in seconds.\n"
  "-s --segment-name <name>     Set the segment name template.\n"
  "-a --audio-init-name <name>  Set the audio initial segment name.\n"
  "-v --video-init-name <name>  Set the video initial segment name.\n"
  "-p --publish-time <time>     Set the publish time to <time> in unix\n"
  "                             timestamp\n"
  "-t --time-url                Set the iso time url.\n"
  "-o --output <path.mpd>       Output mpd info to <path.mpd>.\n"
  "                             stdout will be used if not specified\n"
  << endl;
}

inline bool is_webm(const string & filename)
{
  auto results = split_filename(filename);
  return results.second == "webm" or results.second == "chk";
}

void add_webm_audio(shared_ptr<AudioAdaptionSet> a_set, const string & init,
                    const string & segment, const string & repr_id)
{
  WebmInfo i_info(init);
  WebmInfo s_info(segment);
  uint32_t duration = i_info.get_duration();
  uint32_t timescale = i_info.get_timescale();
  uint32_t bitrate = s_info.get_bitrate(timescale, duration);
  uint32_t sample_rate = i_info.get_sample_rate();

  /* scale the timescale to global timescale */
  uint32_t scaling_factor = global_timescale / timescale;
  duration *= scaling_factor;
  timescale *= scaling_factor;

  auto repr_a = make_shared<AudioRepresentation>(repr_id, bitrate,
        sample_rate, MimeType::Audio_OPUS, timescale, duration);
  a_set->add_repr(repr_a);
}

void add_representation(
    shared_ptr<VideoAdaptionSet> v_set, shared_ptr<AudioAdaptionSet> a_set,
    const string & init, const string & segment)
{
  /* get numbering info from file name
   * we assume the segment name is a number */
  auto seg_path_list = split(segment, "/");
  if (seg_path_list.size() < 2) {
    throw runtime_error(segment + " is in top folder");
  }
  string repr_id = seg_path_list[seg_path_list.size() - 2];

  /* if this is a webm segment */
  if (is_webm(segment)) {
    add_webm_audio(a_set, init, segment, repr_id);
    return;
  }
  /* load mp4 up using parser */
  auto i_parser = make_shared<MP4Parser>(init);
  auto s_parser = make_shared<MP4Parser>(segment);
  i_parser->parse();
  s_parser->parse();
  auto i_info = MP4Info(i_parser);
  auto s_info = MP4Info(s_parser);
  /* find duration, timescale from init and segment individually */
  uint32_t i_duration, s_duration, i_timescale, s_timescale;
  tie(i_timescale, i_duration) = i_info.get_timescale_duration();
  tie(s_timescale, s_duration) = s_info.get_timescale_duration();
  /* selecting the proper values because mp4 atoms are a mess */
  uint32_t duration = s_duration;
  /* override the timescale from init.mp4 */
  uint32_t timescale = s_timescale == 0? i_timescale : s_timescale;
  if (duration == 0) {
    throw runtime_error("Cannot find duration in " + segment);
  }
  /* get bitrate */
  uint32_t bitrate = s_info.get_bitrate(timescale, duration);

  /* scale the timescale to global timescale */
  uint32_t scaling_factor = global_timescale / timescale;
  duration *= scaling_factor;
  timescale *= scaling_factor;

  if (i_info.is_video()) {
    /* this is a video */
    uint16_t width, height;
    uint8_t profile, avc_level;
    tie(width, height) = i_info.get_width_height();
    tie(profile, avc_level) = i_info.get_avc_profile_level();
    /* get fps */
    float fps = s_info.get_fps(timescale, duration);
    auto repr_v = make_shared<VideoRepresentation>(
        repr_id, width, height, bitrate, profile, avc_level, fps, timescale,
        duration);
    v_set->add_repr(repr_v);
  } else {
    /* this is an audio */
    /* TODO: add webm support */
    uint32_t sample_rate = i_info.get_sample_rate();
    uint8_t audio_code;
    uint16_t channel_count;
    tie(audio_code, channel_count) = i_info.get_audio_code_channel();
    /* translate audio code. default AAC_LC 0x40 0x67 */
    MimeType type = MimeType::Audio_AAC_LC;
    if (audio_code == 0x64) { /* I might be wrong about this value */
      type = MimeType::Audio_HE_AAC;
    } else if (audio_code == 0x69) {
      type = MimeType::Audio_MP3;
    }
    auto repr_a = make_shared<AudioRepresentation>(repr_id, bitrate,
        sample_rate, type, timescale, duration);
    a_set->add_repr(repr_a);
  }
}

int main(int argc, char * argv[])
{
  uint32_t buffer_time = default_buffer_time;
  string base_url = default_base_uri;
  string audio_name = default_audio_uri;
  string video_name = default_video_uri;
  string audio_init_name = default_audio_init_uri;
  string video_init_name = default_video_init_uri;
  string time_url = default_time_uri;
  vector<string> dir_list;
  /* default time is when the program starts */
  chrono::seconds publish_time = chrono::seconds(std::time(nullptr));
  string output = "";
  int opt, long_option_index;

  const char *optstring = "u:b:i:e:a:v:o:p:";
  const struct option options[] = {
    {"url",               required_argument, nullptr, 'u'},
    {"buffer-time",       required_argument, nullptr, 'b'},
    {"audio-name",        required_argument, nullptr, 'i'},
    {"video-name",        required_argument, nullptr, 'e'},
    {"audio-init-name",   required_argument, nullptr, 'a'},
    {"video-init-name",   required_argument, nullptr, 'v'},
    {"output",            required_argument, nullptr, 'o'},
    {"publish-time",      required_argument, nullptr, 'p'},
    {"time-url",          required_argument, nullptr, 't'},
    { nullptr,            0,                 nullptr,  0 },
  };

  while (true) {
    opt = getopt_long(argc, argv, optstring, options, &long_option_index);
    if (opt == EOF) {
      break;
    }
    switch (opt) {
      case 'u':
        base_url = optarg;
        break;
      case 'b':
        buffer_time = stoi(optarg);
        break;
      case 'i':
        audio_name = optarg;
        break;
      case 'e':
        video_name = optarg;
        break;
      case 'a':
        audio_init_name = optarg;
        break;
      case 'v':
        video_init_name = optarg;
        break;
      case 'p':
        publish_time = chrono::seconds(stoi(optarg));
        break;
      case 'o':
        output = optarg;
        break;
      case 't':
        time_url = optarg;
        break;
      default:
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
  }

  if (optind >= argc) {
    /* no dir input */
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* check and add to dir_list */
  for (int i = optind; i < argc; i++) {
    string path = argv[i];
    if (not roost::exists(path)) {
      throw runtime_error(path + " does not exist");
      return EXIT_FAILURE;
    } else {
      dir_list.emplace_back(path);
    }
  }

  auto w = make_unique<MPDWriter>(buffer_time, base_url, time_url);

  auto set_v = make_shared<VideoAdaptionSet>(1, video_init_name, video_name);
  auto set_a = make_shared<AudioAdaptionSet>(2, audio_init_name, audio_name);

  /* figure out what kind of representation each folder is */
  for (auto const & path : dir_list) {
    string init_seg_path;
    string seg_path = "";
    string file_extension = "";

    for (const auto & file : roost::get_file_listing(path)) {
      file_extension = split_filename(file).second;
      if (find(media_extension.begin(), media_extension.end(), file_extension)
          != media_extension.end()) {
        seg_path = file;
        break;
      }
    }

    if (file_extension != "m4s") {
      string filename = roost::rbasename(roost::path(audio_init_name)).string();
      init_seg_path = roost::join(path, filename);
    } else {
      /* make an assumption here that media segments (audio and video) have
       * the same extension, i.e., .m4s
       */
      string filename = roost::rbasename(roost::path(video_init_name)).string();
      init_seg_path = roost::join(path, filename);
    }

    if (not roost::exists(init_seg_path)) {
      throw runtime_error("Cannnot find " + init_seg_path);
      return EXIT_FAILURE;
    }

    if (seg_path == "") {
      throw runtime_error("No media segments found in " + path);
    }

    seg_path = roost::join(path, seg_path);
    /* add repr set */
    add_representation(set_v, set_a, init_seg_path, seg_path);
  }

  /* set time */
  w->set_publish_time(publish_time);

  w->add_video_adaption_set(set_v);
  w->add_audio_adaption_set(set_a);

  std::string out = w->flush();

  /* handling output */
  if (output == "") {
    /* print to stdout */
    std::cout << out << std::endl;
  } else {
    FileDescriptor output_fd(CheckSystemCall("open (" + output + ")",
        open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644)));
    output_fd.write(out, true);
    output_fd.close();
  }

  return EXIT_SUCCESS;
}
