#include <iostream>
#include <stdio.h>
#include "fast_ssim.h"

using namespace std;

void print_usage(const string & program_name)
{
  cerr << "Usage: " << program_name << " <video1> <video2> <output>" << endl;
  cerr << endl;
  cerr << "<video1> Video segments. If it is not in Y4M, it will be converted \
to via ffmpeg" << endl;
  cerr << "<video2> Video segments. If it is not in Y4M, it will be converted \
to via ffmpeg" << endl;
  cerr << "<output> Output text file containing SSIM information" << endl;

}

int main(int argc, char * argv[])
{
  char * video_path1;
  char * video_path2;
  char * output_file;
  if(argc != 4) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  video_path1 = argv[1];
  video_path2 = argv[2];
  output_file = argv[3];
  /* open the file
   * fd is used to be consistent with the underlying C library
   */
  FILE* file = fopen(output_file, "w+");
  if (!file)
    throw runtime_error("Unable to open file " + string(output_file));
  fast_ssim(video_path1, video_path2, file);
  fclose(file);
  return 0;
}
