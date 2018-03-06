#include <iostream>
#include <string>
#include <vector>
#include <tuple>

#include "filesystem.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <input_file> "
  "--clean <clean_dir> <clean_ext> [<clean_dir> <clean_ext> ...]\n"
  "       --depend <dep_dir> <dep_ext> [<dep_dir> <dep_ext>]\n"
  "<input_file>   input file from notifier\n"
  "--clean <clean_dir> <clean_ext>  directories and file extensions to clean\n"
  "--depend <dep_dir> <dep_ext>     directories containing dependent files\n"
  "                                 and extensions"
  << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc < 8) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* parse arguments */
  string input_file = argv[1];
  vector<tuple<string, string>> clean_files;
  vector<tuple<string, string>> depend_files;

  int clean_pos = 0, depend_pos = 0;
  for (int i = 2; i < argc; i++) {
    if (string(argv[i]) == "--clean") {
      clean_pos = i;
    } else if (string(argv[i]) == "--depend") {
      depend_pos = i;
    }
  }

  if (clean_pos == 0 or depend_pos == 0) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  int clean_end = 0, depend_end = 0;
  if (clean_pos < depend_pos) {
    clean_end = depend_pos;
    depend_end = argc;
  } else {
    depend_end = clean_pos;
    clean_end = argc;
  }

  for (int i = clean_pos + 1; i < clean_end; i += 2) {
    clean_files.emplace_back(argv[i], argv[i + 1]);
  }

  for (int i = depend_pos + 1; i < depend_end; i += 2) {
    depend_files.emplace_back(argv[i], argv[i + 1]);
  }

  /* check if all dependent files exist */
  string input_filestem = fs::path(input_file).stem();
  for (const auto & depend_file : depend_files) {
    const auto & [dep_dir, dep_ext] = depend_file;

    string dep_filepath = fs::path(dep_dir) / (input_filestem + dep_ext);
    if (not fs::exists(dep_filepath)) {
      return EXIT_SUCCESS;
    }
  }

  /* all of the downstream files exist so we can remove the upstream files */
  error_code ec;
  for (const auto & clean_file : clean_files) {
    const auto & [clean_dir, clean_ext] = clean_file;

    string clean_filepath = fs::path(clean_dir) / (input_filestem + clean_ext);
    /* remove the file to clean and suppress exceptions */
    fs::remove(clean_filepath, ec);
  }

  return EXIT_SUCCESS;
}
