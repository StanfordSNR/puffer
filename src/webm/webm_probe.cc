#include <cstring>
#include <iostream>
#include <memory>
#include <map>
#include "webm_info.hh"
#include "strict_conversions.hh"

using namespace std;

static map<uint32_t, pair<string, char>> print_map = {
  { 0x1a45dfa3, make_pair("EBML Header", 'm') },
  { 0x18538067, make_pair("Segment", 'm') },
  { 0x1549a966, make_pair("Info", 'm') },
  { 0x002ad7b1, make_pair("TimecodeScale", 'u') },
  { 0x1654ae6b, make_pair("Tracks", 'm') },
  { 0x000000d7, make_pair("TrackNumber", 'u') },
  { 0x00000083, make_pair("TrackType", 'u') },
  { 0x000000e7, make_pair("Timecode", 'u') },
  { 0x000000b5, make_pair("SamplingFrequency", 'f') },
  { 0x1f43b675, make_pair("Cluster", 'm') },
  { 0x000000ae, make_pair("TrackEntry", 'm') },
  { 0x000000e1, make_pair("Audio", 'm') },
  { 0x000000e3, make_pair("TrackCombinePlanes", 'm') },
  { 0x1254c367, make_pair("Tags", 'm') },
  { 0x00007373, make_pair("Tag", 'm') },
  { 0x000045a3, make_pair("TagName", 's') },
  { 0x00004487, make_pair("TagString", 's') },
};

void print_usage(const string & name)
{
  cerr << "Usage: " + name + " <filename>" << endl
       << "<filename>       webm file that contains an audio track" << endl;
}

int main(int argc, char * argv[])
{
  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  auto p = make_unique<WebmParser>(string(argv[1]));
  for (const auto & elem : p->get_elements()) {
    uint32_t tag = elem->tag();
    if (print_map.find(tag) == print_map.end()) {
      elem->print();
    } else {
      auto info = print_map[tag];
      string name = info.first;
      char type = info.second;
      uint64_t size = elem->size();
      string data = elem->value();
      switch (type) {
        case 'm':
          cout << "Tag: " << name << endl;
          break;
        case 'f':
          {
            double value;
            if (size == 4) {
              value = read_raw<float>(data, size, true);
            } else {
              value = read_raw<double>(data, size, true);
            }
            cout << "Tag: " << name << " Value: " << value << endl;
          }
          break;
        case 'u':
          {
            uint64_t value = read_raw<uint64_t>(data, size, true);
            cout << "Tag: " << name << " Value: " << dec << value << endl;
          }
          break;
        case 's':
          {
            string s = string(data);
            cout << "Tag: " << name << " Value: " << s << endl;
          }
        default:
          break;
      }
    }
  }
  return EXIT_SUCCESS;
}
