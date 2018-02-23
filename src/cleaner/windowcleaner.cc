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
  "Usage: " << program_name << " <input_file> <clean_ext> <time_window>\n\n"
  "<input_file>   input file from notifier\n"
  "<clean_ext>    extension of the files to clean\n"
  "<time_window>  clean files with names < input_file - time_window"
  << endl;
}

string get_file_basename(const string & path) {
  vector<string> parts = split(path, "/");
  return parts[parts.size() - 1];
}

int main(int argc, char * argv[])
{
  if (argc < 4) {
    print_usage("windowcleaner");
    return EXIT_FAILURE;
  }

  string input_file, clean_dir, clean_ext;
  int64_t time_window;
  input_file = argv[1];
  clean_dir = argv[2];
  clean_ext = argv[3];
  time_window = stoll(argv[4]);

  if (!fs::exists(clean_dir)) {
    cerr << "Directory to clean does not exist " << clean_dir << endl;
    return EXIT_FAILURE;
  }

  string input_file_basename = get_file_basename(input_file);
  string input_file_dir = input_file.substr(0,
                          input_file.length() - input_file_basename.length());
  auto split_input_file_basename = split_filename(input_file_basename);
  if (split_input_file_basename.second != clean_ext) {
    cerr << input_file << " is not of type " << clean_ext << endl;
    return EXIT_SUCCESS;
  }

  /* Obtain the timestamp of the input file from the path */
  int64_t input_file_timestamp = stoll(split_input_file_basename.first);

  for (const auto & entry : fs::directory_iterator(input_file_dir)) {
    string path = entry.path().string();
    string basename = get_file_basename(path);
    auto split_file_basename = split_filename(input_file_basename);

    /* File is of the type to clean */
    if (split_file_basename.second == clean_ext) {
      int64_t file_timestamp = stoll(split_file_basename.first);

      /* Check if file timestamp is outside of the window of the input file */
      if (input_file_timestamp - file_timestamp > time_window) {
        error_code ec;
        if (not fs::remove(path, ec) or ec) {
          cerr << "Warning: file " << path << " cannot be removed" << endl;
        }
      }
    }
  }

  return EXIT_SUCCESS;
}
