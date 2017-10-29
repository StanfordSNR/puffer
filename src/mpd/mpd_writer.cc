#include <getopt.h>
#include <iostream>
#include <string>
#include <memory>
#include "mpd.hh"
#include "path.hh"
#include "mp4_info.hh"

using namespace std;
using namespace MPD;


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

  /* figure out what kind of representation each folder is */
  for (auto const path : dirs) {
    /* find the init mp4 */
    string init_mp4_path = roost::join(path, init_name);
    if (not roost::exists(init_mp4_path)) {
      cerr << "Cannnot find " << init_mp4_path << endl;
      return EXIT_FAILURE;
    }
    /* get all the info except for the duration from init.mp4 */
  }

  auto set_v = make_shared<VideoAdaptionSet>(1, "test1", "test2", 23.976,
          240, 100);
  auto set_a = make_shared<AudioAdaptionSet>(2, "test1", "test2", 240, 100);
  auto repr_v = make_shared<VideoRepresentation>(
    "1", 800, 600, 100000, 100, 20, 23.976);
  auto repr_a = make_shared<AudioRepresentation>("1", 100000, 180000, true);
  set_v->add_repr(repr_v);

  w->add_video_adaption_set(set_v);
  w->add_audio_adaption_set(set_a);

  set_a->add_repr(repr_a);
  std::string out = w->flush();
  std::cout << out << std::endl;

  return 0;
}
