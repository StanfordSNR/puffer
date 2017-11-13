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
  { 0x000067c8, make_pair("SimpleTag", 'm') },
  { 0x000045a3, make_pair("TagName", 's') },
  { 0x00004487, make_pair("TagString", 's') },
};

void print_usage(const string & name)
{
  cerr << "Usage: " + name + " <filename>" << endl
       << "<filename>       webm file that contains an audio track" << endl;
}

void print(shared_ptr<WebmElement> element, uint32_t indent = 0)
{
  for (const auto & elem : element->get_children()) {
    uint32_t tag = elem->tag();
    uint64_t size = elem->size();
    string indent_str = string(indent, ' ');
    if (print_map.find(tag) == print_map.end()) {
      cout << indent_str << "Tag: 0x" << hex << tag << " Size: 0x:"
           << size << endl;
    } else {
      auto info = print_map[tag];
      string name = info.first;
      char type = info.second;
      string data = elem->value();
      switch (type) {
        case 'm':
          cout << indent_str << "Tag: " << name << endl;
          print(elem, indent + 2);
          break;
        case 'f':
          {
            double value;
            if (size == 4) {
              value = read_raw<float>(data, size, true);
            } else {
              value = read_raw<double>(data, size, true);
            }
            cout << indent_str << "Tag: " << name << " Value: " << value
                 << endl;
          }
          break;
        case 'u':
          {
            uint64_t value = read_raw<uint64_t>(data, size, true);
            cout << indent_str << "Tag: " << name << " Value: "
                 << value << endl;
          }
          break;
        case 's':
          {
            string s = string(data);
            cout << indent_str << "Tag: " << name << " Value: " << s << endl;
          }
        default:
          break;
      }
    }
  }
}

int main(int argc, char * argv[])
{
  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  auto p = make_unique<WebmParser>(string(argv[1]));
  for (const auto & elem : p->get_all()) {
    print(elem);
  }
  return EXIT_SUCCESS;
}
