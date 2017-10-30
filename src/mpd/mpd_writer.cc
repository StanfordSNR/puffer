#include <getopt.h>
#include <iostream>
#include <string>
#include <memory>
#include "mpd.hh"
#include "path.hh"
#include "mp4_info.hh"
#include "mp4_parser.hh"
#include "tokenize.hh"

using namespace std;
using namespace MPD;
using namespace MP4;

const char *optstring = "u:p:b:s:i:";
const struct option options[] = {
  {"url", required_argument, NULL, 'u'},
  {"update-period", required_argument, NULL, 'p'},
  {"buffer-time", required_argument, NULL, 'b'},
  {"segment-name", required_argument, NULL, 's'},
  {"init-name", required_argument, NULL, 'i'},
  {NULL, 0, NULL, 0},
};

const char default_base_uri[] = "/";
const char default_media_uri[] = "$Number$.m4s";
const char default_init_uri[] = "init.mp4";
const uint32_t default_update_period = 60;
const uint32_t default_buffer_time = 2;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " [options] <dir> <dir> ...\n\n"
       << "<dir>                        Directory where media segments are stored" << endl
       << "-u --url <base_url>          Set the base url for all media segments." << endl
       << "-p --update-period <period>  Set the update period in seconds." << endl
       << "-b --buffer-time <time>      Set the minimum buffer time in seconds."
       << "-s --segment-name <name>     Set the segment name template." << endl
       << "-i --init-name <name>        Set the initial segment name." << endl
       << endl;
}

void add_representation(shared_ptr<VideoAdaptionSet> v_set,
    shared_ptr<AudioAdaptionSet> a_set, const string & init,
        const string & segment)
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
  if ( duration == 0) {
    throw runtime_error("Cannot find duration in " + segment);
  }
  /* get bitrate */
  uint32_t bitrate = s_info.get_bitrate(timescale, duration);
  /* get fps */
  if (i_info.is_video()) {
    /* this is a video */
    uint16_t width, height;
    uint8_t profile, avc_level;
    tie(width, height) = i_info.get_width_height();
    tie(profile, avc_level) = i_info.get_avc_profile_level();
    float fps = s_info.get_fps(timescale, duration);
    /* id will be set later */
    auto repr_v = make_shared<VideoRepresentation>(
        "", width, height, bitrate, profile, avc_level, fps, timescale,
        duration);
    v_set->add_repr(repr_v);
  } else {
    /* this is an audio */
    /* TODO: add audio support */
    auto repr_a = make_shared<AudioRepresentation>("1", 100000, 180000, true,
        timescale, duration);
    a_set->add_repr(repr_a);
  }
}

void set_repr_id(shared_ptr<AdaptionSet> set)
{
  uint32_t i = 1;
  for (auto & repr : set->get_repr())
  {
    repr->id = to_string(i);
    i++;
  }
}

int main(int argc, char * argv[])
{
  int c, long_option_index;
  uint32_t update_period = default_update_period;
  uint32_t buffer_time = default_buffer_time;
  string base_url = default_base_uri;
  string segment_name = default_media_uri;
  string init_name = default_init_uri;
  vector<string> dirs;

  while ((c = getopt_long(argc, argv, optstring, options, &long_option_index))
      != EOF) {
    switch (c) {
      case 'u': base_url = optarg; break;
      case 'p': update_period = stoi(optarg); break;
      case 'l': buffer_time = stoi(optarg); break;
      case 's': segment_name = optarg; break;
      case 'i': init_name = optarg; break;
      default: {
                print_usage(argv[0]);
                return EXIT_FAILURE;
               }
    }
  }
  if (optind == argc) {
    /* no dir input */
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* check and add to dirs */
  for (int i = optind; i < argc; i++) {
    string path = argv[i];
    if (not roost::exists(path)) {
      cerr << path << " does not exist" << endl;
      return EXIT_FAILURE;
    } else {
      dirs.emplace_back(path);
    }
  }

  auto w = make_unique<MPDWriter>(update_period, buffer_time, base_url);
  auto set_v = make_shared<VideoAdaptionSet>(1, init_name, segment_name);
  auto set_a = make_shared<AudioAdaptionSet>(2, init_name, segment_name);

  /* figure out what kind of representation each folder is */
  for (auto const path : dirs) {
    /* find the init mp4 */
    string init_mp4_path = roost::join(path, init_name);
    if (not roost::exists(init_mp4_path)) {
      cerr << "Cannnot find " << init_mp4_path << endl;
      return EXIT_FAILURE;
    }
    /* trying to find a m4s segment. It does not necessary start from 0 */
    string m4s = "";
    for (const auto & m4s_path : roost::get_directory_listing(path)) {
      if (split_filename(m4s_path).second == "m4s") {
        m4s = m4s_path;
        break;
      }
    }
    if (m4s == "") {
      /* no m4s file found */
      throw runtime_error("No m4s file found in " + path);
    }
    string m4s_path = roost::join(path, m4s);
    /* add repr set */
    add_representation(set_v, set_a, init_mp4_path, m4s_path);
  }

  set_repr_id(set_v);
  set_repr_id(set_a);

  /* below is for testing purpose
   * will be removed when mpd_writer is done */
  w->add_video_adaption_set(set_v);
  w->add_audio_adaption_set(set_a);

  std::string out = w->flush();
  std::cout << out << std::endl;

  return 0;
}
