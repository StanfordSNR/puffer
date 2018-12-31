#include <fcntl.h>
#include <cmath>

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

  /* the overall SSIM appears between "All:" and the first space after */
  auto ssim_pos = output.rfind("All:");
  if (ssim_pos == string::npos) {
    cerr << "No valid SSIM found in the output of FFmpeg" << endl;
    return EXIT_FAILURE;
  }
  ssim_pos += 4;

  auto space_pos = output.find(' ', ssim_pos);
  if (space_pos == string::npos) {
    cerr << "No valid SSIM found in the output of FFmpeg" << endl;
    return EXIT_FAILURE;
  }
  string ssim_str = output.substr(ssim_pos, space_pos - ssim_pos);

  /* check if ssim_str is a valid SSIM between -1 and 1 */
  try {
    double ssim_val = stod(ssim_str);
    if (ssim_val >= -1 and ssim_val <= 1) {
      cerr << "SSIM = " + ssim_str + " between " + video1 + " and " + video2
           << endl;
    } else {
      cerr << "Invalid SSIM value out of range: " + ssim_str << endl;
      return EXIT_FAILURE;
    }
  } catch (const exception & e) {
    cerr << "Error in converting " + ssim_str + ": " + e.what() << endl;
    return EXIT_FAILURE;
  }

  /* write the SSIM value to output_path */
  write_to_file(output_path, ssim_str);

  return EXIT_SUCCESS;
}
