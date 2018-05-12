#include <getopt.h>
#include <time.h>
#include <iostream>
#include <string>
#include <chrono>
#include <memory>
#include <set>
#include <algorithm>
#include <queue>
#include <numeric>

#include "mpd.hh"
#include "mp4_info.hh"
#include "mp4_parser.hh"
#include "tokenize.hh"
#include "file_descriptor.hh"
#include "exception.hh"
#include "strict_conversions.hh"
#include "webm_info.hh"
#include "filesystem.hh"

using namespace std;
using namespace MPD;
using namespace MP4;

const uint32_t global_timescale = 90000;

const char default_base_uri[] = "";
const char default_audio_uri[] = "$RepresentationID$/$Time$.chk";
const char default_video_uri[] = "$RepresentationID$/$Time$.m4s";
const char default_video_init_uri[] = "$RepresentationID$/init.mp4";
const char default_audio_init_uri[] = "$RepresentationID$/init.webm";
const uint32_t default_buffer_time = 2;
const string default_time_uri = "/time";
const uint32_t default_num_audio_check = 3;

const set<fs::path> media_extension {".m4s", ".chk"};

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
  "-n --num-audio               Number of webm audio chunks to check.\n"
  "-o --output <path.mpd>       Output mpd info to <path.mpd>.\n"
  "                             stdout will be used if not specified\n"
  << endl;
}

inline bool is_webm(const fs::path & filename)
{
  return filename.extension() == ".webm" \
      or filename.extension() == ".chk";
}

void add_webm_audio(shared_ptr<AudioAdaptionSet> a_set, const fs::path & init,
                    const fs::path & segment, const string & repr_id,
                    const uint32_t expected_duration)
{
  WebmInfo i_info(init);
  WebmInfo s_info(segment);

  /* get webm info */
  uint32_t timescale = i_info.get_timescale();
  uint32_t duration = i_info.get_duration(timescale);

  /* compute the expected duration and actual duration */
  float f_duration = duration / (float)(timescale);
  float f_expected = expected_duration / (float)global_timescale;
  if (f_duration != f_expected and expected_duration) {
    cerr << "WARN: expect to find duration " << f_expected
         << ". got " << f_duration << endl;
  }

  uint32_t sample_rate = i_info.get_sample_rate();

  /* scale the timescale to global timescale */
  float scaling_factor = static_cast<float>(global_timescale) / timescale;
  timescale = global_timescale;
  duration = narrow_round<uint32_t>(duration * scaling_factor);

  if (expected_duration) {
    duration = expected_duration;
  }

  uint32_t bitrate = s_info.get_bitrate(timescale, duration);
  auto repr_a = make_shared<AudioRepresentation>(repr_id, bitrate,
        sample_rate, MimeType::Audio_OPUS, timescale, duration);
  a_set->add_repr(repr_a);
}

void add_mp4_representation(
    shared_ptr<VideoAdaptionSet> v_set, shared_ptr<AudioAdaptionSet> a_set,
    const fs::path & init, const fs::path & segment, const string & repr_id,
    uint32_t expected_duration)
{
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

  /* get bitrate */
  uint32_t bitrate = s_info.get_bitrate(timescale, duration);

  float f_duration = duration / (float)(timescale);
  float f_expected = expected_duration / (float)global_timescale;
  if (f_duration != f_expected and expected_duration) {
    cerr << "WARN: expect to find duration " << f_expected
         << ". got " << f_duration << endl;
  }

  /* scale the timescale to global timescale */
  float scaling_factor = static_cast<float>(global_timescale) / timescale;
  timescale = global_timescale;
  duration = narrow_round<uint32_t>(duration * scaling_factor);

  if (expected_duration) {
    duration = expected_duration;
  }

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
                                                   sample_rate, type,
                                                   timescale, duration);
    a_set->add_repr(repr_a);
  }
}

void add_representation(
    shared_ptr<VideoAdaptionSet> v_set, shared_ptr<AudioAdaptionSet> a_set,
    const fs::path & init, const fs::path & segment,
    const uint32_t expected_duration)
{
  /* get repr id from it's parent folder.
   * for instance, if the segment path is a/b/0.m4s,
   * then we will have b
   */
  fs::path repr_id = *(--(--segment.end()));
  if (repr_id.empty()) {
    throw runtime_error(segment.string() + " is in top folder");
  }

  /* if this is a webm segment */
  if (is_webm(segment)) {
    add_webm_audio(a_set, init, segment, repr_id, expected_duration);
    return;
  } else {
    add_mp4_representation(v_set, a_set, init, segment, repr_id,
                           expected_duration);
  }
}

int main(int argc, char * argv[])
{
  uint32_t buffer_time = default_buffer_time;
  string base_url = default_base_uri;
  string audio_name = default_audio_uri;
  string video_name = default_video_uri;
  fs::path audio_init_name = default_audio_init_uri;
  fs::path video_init_name = default_video_init_uri;
  string time_url = default_time_uri;
  vector<fs::path> dir_list;
  uint32_t num_audio_check = default_num_audio_check;

  /* default time is when the program starts */
  chrono::seconds publish_time = chrono::seconds(std::time(nullptr));
  string output = "";
  int opt, long_option_index;

  const char *optstring = "u:b:i:e:a:v:o:p:t:n:";
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
    {"num-audio",         required_argument, nullptr, 'n'},
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
      case 'n':
        num_audio_check = stoi(optarg);
        break;
      case 't':
        time_url = optarg;
        break;
      default:
        break; /* ignore unexpected arguments */
        // print_usage(argv[0]);
        // return EXIT_FAILURE;
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
    if (not fs::exists(path)) {
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
    fs::path init_seg_path;
    fs::path seg_path;
    fs::path file_extension;
    uint32_t expected_duration = 0;
    priority_queue<int> queue;
    for (const auto & p : fs::directory_iterator(path)) {
      fs::path filename = p.path().filename();
      file_extension = filename.extension();
      if (find(media_extension.begin(), media_extension.end(), file_extension)
          != media_extension.end()) {
        int file_num = stoi(filename.stem().string());

        /* set the seg_path only once */
        if (seg_path.empty()) {
          seg_path = p.path();
        }
        if (queue.size() >= num_audio_check) {
          if (file_num < queue.top()) {
            /* find an earlier segment */
            queue.pop();
            queue.emplace(file_num);
          }
        } else {
          queue.emplace(file_num);
        }
      }
    }

    /* compute the expected duration */
    expected_duration = queue.top();
    while (queue.size()) {
      expected_duration = gcd(queue.top(), expected_duration);
      queue.pop();
    }

    if (file_extension != ".m4s" and file_extension != ".mp4") {
      fs::path filename = audio_init_name.filename();
      init_seg_path = path / filename;
    } else {
      /* make an assumption here that media segments (audio and video) have
       * the same extension, i.e., .m4s
       */
      fs::path filename = video_init_name.filename();
      init_seg_path = path  / filename;
    }

    if (not fs::exists(init_seg_path)) {
      throw runtime_error("Cannnot find " + init_seg_path.string());
      return EXIT_FAILURE;
    }

    if (seg_path == "") {
      throw runtime_error("No media segments found in " + path.string());
    }

    /* add repr set */
    add_representation(set_v, set_a, init_seg_path, seg_path,
                       expected_duration);
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
