#include <iostream>
#include <string>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>
#include <regex>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

void print_usage(char * program_name)
{
  cerr << "Usage: " << program_name << " [options]" << endl;
  cerr << "\t-d <dir> -dir=<dir>        clean everything in <dir>" << endl;
  cerr << "\t-p <patt> -pattern=<patt>  file name with <patt> will be cleaned.\
 <patt> is a regular expression." << endl;
  cerr << "\t-t <time> -time=<time>     file older than current time by \
<time> seconds will be cleaned" << endl;
}

const char *optstring = "d:p:t:";
const struct option options[] = {
  {"dir", required_argument, NULL, 'd'},
  {"pattern", required_argument, NULL, 'p'},
  {"time", required_argument, NULL, 't'},
  {NULL,0,NULL,0}
};

int main(int argc, char * *argv)
{
  int c;
  int long_option_index;
  char * dir_name = NULL;
  char * pattern = NULL;
  int time_diff = 0;
  time_t now;
  cmatch m;

  if(argc != 7) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  while((c=getopt_long(argc,argv,optstring,options,&long_option_index))!=EOF){
    switch(c){
      case 'd': dir_name = optarg; break;
      case 'p': pattern = optarg; break;
      case 't': time_diff = atoi(optarg); break;
      default: {
                print_usage(argv[0]);
                return EXIT_FAILURE;
               }
    }
  }

  if(dir_name == NULL || pattern == NULL) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* compile regex */
  regex re_file(pattern);

  /* get current time */
  time(&now);

  DIR *directory = opendir(dir_name);

  if(directory == NULL)
  {
    cerr << "Unable to open directory " << dir_name;
    return EXIT_FAILURE;
  }

  struct dirent *entry;
  while(NULL != ( entry = readdir(directory)))
  {
    char * filename = entry->d_name;
    string fullpath = string(dir_name) + "/" + string(filename);
    if(regex_match(filename, m, re_file)) {
      /* get file stats and compare with current time */
      struct stat f_stat;
      if(stat(fullpath.c_str(), &f_stat)) {
        cerr << "Unable to get file stats of " << fullpath << endl;
        continue;
      }
      /* compare the time */
      time_t mod_time = f_stat.st_mtim.tv_sec;
      if(abs(difftime(now, mod_time)) < time_diff) {
        cout << "Skip " << fullpath << endl;
        continue; /* file is still relatively new */
      }
      /* delete files */
      if(remove(fullpath.c_str()))
        cerr << "Unable to delete file " << fullpath << endl;
      else
        cout << fullpath << " deleted" << endl;
    } else {
      cout << "Patten not matched. Skip " << fullpath << endl;
    }
  }

  return EXIT_SUCCESS;
}
