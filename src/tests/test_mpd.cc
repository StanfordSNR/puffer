#include "mpd.hh"
#include <string>
#include <iostream>
#include <memory>

int main()
{
  auto w = std::make_unique<MPDWriter>(60, "/video");
  MPD::VideoAdaptionSet set_v = MPD::VideoAdaptionSet(1, "test1", "test2", 24, 240, 100);
  MPD::AudioAdaptionSet set_a = MPD::AudioAdaptionSet(2, "test1", "test2", 240, 100);
  MPD::VideoRepresentation repr_v = MPD::VideoRepresentation(
    "1", 800, 600, 100000, MPD::ProfileLevel::High, 20, 24);
  MPD::AudioRepresentation repr_a = MPD::AudioRepresentation(
    "1", 100000, 180000);
  set_v.add_repr(&repr_v);
  w->add_adaption_set(&set_v);
  w->add_adaption_set(&set_a);
  /* re-order
   * if reference is used, set will copy the value, hence re-ordering won't work*/
  set_a.add_repr(&repr_a);
  std::string out = w->flush();
  std::cout << out << std::endl;
  return 0;
}
