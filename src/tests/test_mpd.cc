#include "mpd.hh"
#include <string>
#include <iostream>
#include <memory>

using namespace std;
using namespace MPD;

int main()
{
  auto w = std::make_unique<MPDWriter>(60, 2, "/video");
  auto set_v = VideoAdaptionSet(1, "test1", "test2", 23.976, 240, 100);
  auto set_a = AudioAdaptionSet(2, "test1", "test2", 240, 100);
  auto repr_v = VideoRepresentation(
    "1", 800, 600, 100000, MPD::ProfileLevel::High, 20, 23.976);
  auto repr_a = AudioRepresentation("1", 100000, 180000, true);
  set_v.add_repr(move(repr_v));

  w->add_video_adaption_set(move(set_v));
  w->add_audio_adaption_set(move(set_a));

  set_a.add_repr(move(repr_a));
  std::string out = w->flush();
  std::cout << out << std::endl;
  return 0;
}
