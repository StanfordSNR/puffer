#include <sys/stat.h>
#include <iostream>
#include <string>
#include <vector>
#include <system_error>

#include "filesystem.hh"
#include "tokenize.hh"
#include "exception.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <input_file> <remove_dir> <remove_ext> [depdir1 [depdir2 [depdir3] ...]]\n\n"
  "<input_file>   input file from notifier\n"
  "<remove_dir>   output dir from notifier\n"
  "<remove_ext>   extension of the file to remove if all downstream dependencies exist\n"
  "[depdir ... ]  directories containing dependent files"
  << endl;
}

string get_file_basename(const string & path) {
  vector<string> parts = split(path, "/");
  return parts[parts.size() - 1];
}

int main(int argc, char * argv[])
{
  if (argc < 5) {
    print_usage("depcleaner");
    return EXIT_FAILURE;
  }

  string input_file, remove_dir, remove_ext;
  input_file = argv[1];
  remove_dir = argv[2];
  remove_ext = argv[3];

  if (!fs::exists(remove_dir)) {
    cerr << "Output directory does not exist " << remove_dir << endl;
    return EXIT_FAILURE;
  }

  string input_file_basename = get_file_basename(input_file);

  for (int i = 4; i < argc; i++) {
    string dependent_dir = argv[i];
    string dependent_file = string(dependent_dir) + "/" + input_file_basename;
    if (!fs::exists(dependent_file)) {
      /* one of the dependent files does not exist yet so we do nothing */
      return EXIT_SUCCESS;
    }
  }

  string input_file_no_ext = split_filename(input_file_basename).first;

  /* all of the downstream files exist so we can remove the upstream files */
  string file_to_remove = remove_dir + "/" + input_file_no_ext + "." + remove_ext;
  error_code ec;
  if (not fs::remove(file_to_remove, ec) or ec) {
    cerr << "Warning: file " << file_to_remove << " cannot be removed" << endl;
  }

  return EXIT_SUCCESS;
}
