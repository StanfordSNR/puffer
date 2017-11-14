#include <cstring>
#include <iostream>
#include <memory>
#include <map>

#include "webm_info.hh"
#include "strict_conversions.hh"

using namespace std;

static map<uint32_t, pair<string, char>> print_map = {
  { ElementTagID::EBML, make_pair("EBML Header", 'm') },
  { ElementTagID::Segment, make_pair("Segment", 'm') },
  { ElementTagID::Info, make_pair("Info", 'm') },
  { ElementTagID::TimecodeScale, make_pair("TimecodeScale", 'u') },
  { ElementTagID::Tracks, make_pair("Tracks", 'm') },
  { ElementTagID::TrackNumber, make_pair("TrackNumber", 'u') },
  { ElementTagID::TrackType, make_pair("TrackType", 'u') },
  { ElementTagID::Timecode, make_pair("Timecode", 'u') },
  { ElementTagID::SamplingFrequency, make_pair("SamplingFrequency", 'f') },
  { ElementTagID::Cluster, make_pair("Cluster", 'm') },
  { ElementTagID::TrackEntry, make_pair("TrackEntry", 'm') },
  { ElementTagID::Audio, make_pair("Audio", 'm') },
  { ElementTagID::TrackCombinePlanes, make_pair("TrackCombinePlanes", 'm') },
  { ElementTagID::Tags, make_pair("Tags", 'm') },
  { ElementTagID::Tag, make_pair("Tag", 'm') },
  { ElementTagID::SimpleTag, make_pair("SimpleTag", 'm') },
  { ElementTagID::TagName, make_pair("TagName", 's') },
  { ElementTagID::TagString, make_pair("TagString", 's') },
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
