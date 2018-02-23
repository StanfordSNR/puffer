#include <iostream>
#include <string>
#include "yaml-cpp/yaml.h"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <YAML configuration>"
  << endl;
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  YAML::Node config = YAML::LoadFile(argv[1]);

  const YAML::Node & ress = config["video"];
  for (YAML::const_iterator res = ress.begin(); res != ress.end(); ++res) {
    cout << res->first.as<string>() << ": ";

    const YAML::Node & crfs = res->second;
    for (YAML::const_iterator crf = crfs.begin(); crf != crfs.end(); ++crf) {
      cout << crf->as<int>() << " ";
    }

    cout << endl;
  }

  const YAML::Node & bitrates = config["audio"];
  for (YAML::const_iterator bitrate = bitrates.begin();
       bitrate != bitrates.end(); ++bitrate) {
    cout << bitrate->as<string>() << " ";
  }
  cout << endl;

  return EXIT_SUCCESS;
}
