#include <getopt.h>
#include <time.h>
#include <iostream>
#include <string>
#include <chrono>
#include <memory>

#include "mpd.hh"
#include "path.hh"
#include "mp4_info.hh"
#include "mp4_parser.hh"
#include "tokenize.hh"
#include "file_descriptor.hh"
#include "exception.hh"
#include "strict_conversions.hh"

using namespace std;
using namespace MPD;
using namespace MP4;

const char *optstring = "u:b:s:i:a:v:o:l:";
const struct option options[] = {
  {"url", required_argument, NULL, 'u'},
  {"buffer-time", required_argument, NULL, 'b'},
  {"segment-name", required_argument, NULL, 's'},
  {"init-name", required_argument, NULL, 'i'},
  {"audio-start", required_argument, NULL, 'a'},
  {"video-start", required_argument, NULL, 'v'},
  {"output", required_argument, NULL, 'o'},
  {"publish-time", required_argument, NULL, 'l'},
  {NULL, 0, NULL, 0},
};

const char default_base_uri[] = "/";
const char default_media_uri[] = "$RepresentationID$/$Number$.m4s";
const char default_init_uri[] = "$RepresentationID$/init.mp4";
const uint32_t default_buffer_time = 2;
const uint32_t default_seg_start = 0;

/* since we are faking live streaming with static mpd, this value has to be
 * as big as possible to prevent the client from finishing playing the stream,
 * although it is highly unlikely (no one is going to have the browser running
 * for years */
const uint32_t media_duration = 0xFFFFFFFF;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " [options] <dir> <dir> ...\n\n"
       << "<dir>                        Directory where media segments are stored" << endl
       << "-u --url <base_url>          Set the base url for all media segments." << endl
       << "-b --buffer-time <time>      Set the minimum buffer time in seconds."
       << "-s --segment-name <name>     Set the segment name template." << endl
       << "-i --init-name <name>        Set the initial segment name." << endl
       << "-a --audio-start <num>       Set the audio segment start number as <num>" << endl
       << "-v --video-start <num>       Set the video segment start number as <num>" << endl
       << "-l --publish-time <time>     Set the publish time to <time> in unix timestamp" << endl
       << "-o --output <path.mpd>       Output mpd info to <path.mpd>. stdout will be used if not specified" << endl
       << endl;
}

void add_representation(string repr_id,
    shared_ptr<VideoAdaptionSet> v_set, shared_ptr<AudioAdaptionSet> a_set,
    const string & init, const string & segment)
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
    auto repr_v = make_shared<VideoRepresentation>(
        repr_id, width, height, bitrate, profile, avc_level, fps, timescale,
        duration);
    v_set->add_repr(repr_v);
  } else {
    /* this is an audio */
    /* TODO: add audio support */
    auto repr_a = make_shared<AudioRepresentation>(repr_id, 100000, 180000, true,
        timescale, duration);
    a_set->add_repr(repr_a);
  }
}

int main(int argc, char * argv[])
{
  int c, long_option_index;
  uint32_t buffer_time = default_buffer_time;
  uint32_t a_start = default_seg_start;
  uint32_t v_start = default_seg_start;
  string base_url = default_base_uri;
  string segment_name = default_media_uri;
  string init_name = default_init_uri;
  vector<string> dirs;
  /* default time is when the program starts */
  chrono::seconds publish_time = chrono::seconds(std::time(NULL));
  string output = "";

  while ((c = getopt_long(argc, argv, optstring, options, &long_option_index))
      != EOF) {
    switch (c) {
      case 'u': base_url = optarg; break;
      case 'b': buffer_time = stoi(optarg); break;
      case 's': segment_name = optarg; break;
      case 'i': init_name = optarg; break;
      case 'a': a_start = stoi(optarg); break;
      case 'v': v_start = stoi(optarg); break;
      case 'l': publish_time = chrono::seconds(stoi(optarg)); break;
      case 'o': output = optarg; break;
      default : {
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

  auto w = make_unique<MPDWriter>(media_duration, buffer_time, base_url);
  w->set_audio_start_number(a_start);
  w->set_video_start_number(v_start);

  auto set_v = make_shared<VideoAdaptionSet>(1, init_name, segment_name);
  auto set_a = make_shared<AudioAdaptionSet>(2, init_name, segment_name);

  /* figure out what kind of representation each folder is */
  for (auto const & path : dirs) {
    /* find the init mp4 */
    string init_mp4_path = roost::join(path, "init.mp4");
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
    add_representation(path, set_v, set_a, init_mp4_path, m4s_path);
  }

  /* set time */
  w->set_publish_time(publish_time);

  /* compute for the offset time */
  uint32_t v_duration = set_v->duration();
  uint32_t v_timescale = set_v->timescale();
  uint32_t a_duration = set_a->duration();
  uint32_t a_timescale = set_a->timescale();

  if (v_timescale != 0 and a_timescale != 0) {
    /* we have both video and audio */
    /* calculate how long the segments have been playing */
    double v_time = ((double)v_duration) * v_start / v_timescale;
    double a_time = ((double)a_duration) * a_start / a_timescale;
    if (v_time > a_time) {
      double time_offset = v_time - a_time;
      uint32_t offset = narrow_cast<uint32_t>(time_offset);
      set_v->set_presentation_time_offset(offset);
    } else if (v_time < a_time) {
      double time_offset = a_time - v_time;
      uint32_t offset = narrow_cast<uint32_t>(time_offset);
      set_a->set_presentation_time_offset(offset);
    }
  }

  w->add_video_adaption_set(set_v);

  /* below is for testing purpose
   * will be removed when mpd_writer is done */

  std::string out = w->flush();

  /* handling output */
  if (output == "") {
    /* print to stdout */
    std::cout << out << std::endl;
  } else {
    FileDescriptor output_fd(
        CheckSystemCall("open (" + output + ")",
          open(output.c_str(), O_WRONLY | O_CREAT, 0644)));
    output_fd.write(out, true);
    output_fd.close();
  }

  return EXIT_SUCCESS;
}
