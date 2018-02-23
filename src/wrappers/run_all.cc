#include <iostream>
#include <string>
#include "yaml-cpp/yaml.h"

using namespace std;

int main()
{
  YAML::Node config = YAML::LoadFile("config.yml");

  const YAML::Node & ress = config["test"]["video"];
  for (YAML::const_iterator res = ress.begin(); res != ress.end(); ++res) {
    cout << res->first.as<string>() << ": ";

    const YAML::Node & crfs = res->second;
    for (YAML::const_iterator crf = crfs.begin(); crf != crfs.end(); ++crf) {
      cout << crf->as<int>() << " ";
    }

    cout << endl;
  }

  return EXIT_SUCCESS;
}
