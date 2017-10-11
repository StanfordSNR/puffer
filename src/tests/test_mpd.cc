#include "mpd.hh"
#include <string>
#include <iostream>
#include <memory>

int main(int argv, char* argc[])
{
  auto w = std::make_unique<MPDWriter>(60, "/video");
  MPD::AdaptionSet set = MPD::AdaptionSet(1, "test1", "test2", 24, 240);
  MPD::Representation repr = MPD::Representation{
    "a1", 800, 600, 100000, MPD::ProfileLevel::High, 20, MPD::MimeType::Video, 24
  };
  set.add_repr(repr);
  w->add_adaption_set(set);
  std::string out = w->flush();

  return 0;
}
