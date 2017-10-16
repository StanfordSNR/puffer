AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = mp4_structure

mp4_structure_SOURCES = mp4_structure.cc \
  mp4_parser.hh mp4_parser.cc \
  mp4_box.hh mp4_box.cc \
  mp4_file.hh mp4_file.cc
mp4_structure_LDADD = ../util/libutil.a
