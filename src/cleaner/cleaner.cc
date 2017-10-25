#include <iostream>
#include <string>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>
#include <regex>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>

#include "path.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " [options]" << endl;
  cerr << "\t-d <dir> -dir=<dir>        clean everything in <dir>" << endl;
  cerr << "\t-p <patt> -pattern=<patt>  file name with <patt> will be cleaned.\
 <patt> is a regular expression." << endl;
  cerr << "\t-t <time> -time=<time>     file older than current time by \
<time> seconds will be cleaned" << endl;
}

int main(int argc, char * *argv)
{
  int c;
  int long_option_index;
  string dir_name;
  string pattern;
  string program_name = string(argv[0]);
  int time_diff = 0;
  time_t now;
  cmatch m;

  const string optstring = "d:p:t:";
  const struct option options[] = {
    {"dir", required_argument, NULL, 'd'},
    {"pattern", required_argument, NULL, 'p'},
    {"time", required_argument, NULL, 't'},
    {NULL,0,NULL,0}
  };


  if(argc != 7) {
    print_usage(program_name);
    return EXIT_FAILURE;
  }

  while((c = getopt_long(argc, argv, optstring.c_str(), options,
          &long_option_index)) != EOF){
    switch(c){
      case 'd': dir_name = optarg; break;
      case 'p': pattern = optarg; break;
      case 't': time_diff = atoi(optarg); break;
      default: {
                print_usage(program_name);
                return EXIT_FAILURE;
               }
    }
  }

  if(!dir_name.length() || !pattern.length()) {
    print_usage(program_name);
    return EXIT_FAILURE;
  }

  /* compile regex */
  const regex re_file(pattern);

  /* get current time */
  time(&now);

  if(!roost::is_directory(dir_name)) {
    cerr << "Unable to open directory " << dir_name;
    return EXIT_FAILURE;
  }

  for(auto filename : roost::get_directory_listing(dir_name)) {
    const string fullpath = roost::join(dir_name, filename);
    if(regex_match(filename.c_str(), m, re_file)) {
      /* get file stats and compare with current time */
      struct stat f_stat;
      if(stat(fullpath.c_str(), &f_stat)) {
        cerr << "Unable to get file stats of " << fullpath << endl;
        continue;
      }
      /* compare the time */
      const time_t mod_time = f_stat.st_mtim.tv_sec;
      if(std::abs(difftime(now, mod_time)) < time_diff) {
        cout << "Skip " << fullpath << endl;
        continue; /* file is still relatively new */
      }
      /* delete files */
      if(!roost::remove(fullpath.c_str()))
        cerr << "Unable to delete file " << fullpath << endl;
      else
        cout << fullpath << " deleted" << endl;
    } else {
      cout << "Patten not matched. Skip " << fullpath << endl;
    }
  }

  return EXIT_SUCCESS;
}
