#include <fcntl.h>

#include <iostream>
#include <string>
#include <stdexcept>

#include "file_descriptor.hh"
#include "system_runner.hh"
#include "exception.hh"

using namespace std;

void print_usage(const string & program)
{
  cerr <<
  "Usage: " << program << " <video1.y4m> <video2.y4m> <output>"
  << endl;
}

void write_to_file(const string & output_path, const string & result)
{
  FileDescriptor output_fd(CheckSystemCall("open (" + output_path + ")",
      open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644)));
  output_fd.write(result);
  output_fd.close();
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

  string video1{argv[1]}, video2{argv[2]}, output_path{argv[3]};

  /* run FFmpeg's SSIM calculation and read from stderr */
  vector<string> cmd {
    "ffmpeg", "-nostdin", "-hide_banner", "-i", video1, "-i", video2,
    "-lavfi", "ssim", "-threads", "1", "-f", "null", "-" };
  string output = run("ffmpeg", cmd, false, true).second;

  /* the overall SSIM should appear within the last pair of parentheses */
  int last_left_parenthesis = -1, last_right_parenthesis = -1;

  for (int i = output.size() - 1; i >= 0; i--) {
    const auto & c = output.at(i);

    if (c == ')') {
      last_right_parenthesis = i;
    } else if (c == '(') {
      last_left_parenthesis = i;
    }

    if (last_left_parenthesis != -1 and last_right_parenthesis != -1) {
      break;
    }
  }

  if (last_left_parenthesis == -1 or last_right_parenthesis == -1) {
    cerr << "No SSIM found in the output" << endl;
    return EXIT_FAILURE;
  }

  int cnt = last_right_parenthesis - last_left_parenthesis - 1;
  string ssim_str = output.substr(last_left_parenthesis + 1, cnt);

	/* check if ssim_str is a valid double */
  stod(ssim_str);
  cerr << "SSIM = " + ssim_str + " between " + video1 + " and " + video2
       << endl;

  /* write the SSIM value to output_path */
  write_to_file(output_path, ssim_str);

  return EXIT_SUCCESS;
}
