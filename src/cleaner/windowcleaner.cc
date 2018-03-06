#include <iostream>
#include <string>
#include <vector>

#include "filesystem.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <input_file> <clean_ext> <time_window>\n\n"
  "<input_file>   input file from notifier\n"
  "<clean_ext>    extension of the files to clean\n"
  "<time_window>  clean files with timestamped names that are less than\n"
  "               input_file - time_window"
  << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 4) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string input_file = argv[1];
  string clean_ext = argv[2];
  int64_t time_window = stoll(argv[3]);

  if (time_window <= 0) {
    cerr << "Time window cannot be negative or less than 0" << endl;
    return EXIT_FAILURE;
  }

  /* Obtain the timestamp of the input file from the path */
  int64_t input_file_timestamp = stoll(fs::path(input_file).stem());

  const auto & input_dir = fs::path(input_file).parent_path();

  error_code ec;
  for (const auto & entry : fs::directory_iterator(input_dir)) {
    const auto & file = entry.path();
    /* File is of the type to clean */
    if (file.extension() != clean_ext) {
      continue;
    }

    int64_t file_timestamp = stoll(file.stem());

    /* Check if file timestamp is outside of the window of the input file */
    if (input_file_timestamp - file_timestamp > time_window) {
      /* remove the file and suppress exceptions */
      fs::remove(file, ec);
    }
  }

  return EXIT_SUCCESS;
}
