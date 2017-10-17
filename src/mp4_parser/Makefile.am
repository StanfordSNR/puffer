AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

COMMON_SOURCES = \
  mp4_parser.hh mp4_parser.cc \
  mp4_box.hh mp4_box.cc \
  mp4_file.hh mp4_file.cc

bin_PROGRAMS = mp4_structure mp4_split

mp4_structure_SOURCES = mp4_structure.cc $(COMMON_SOURCES)
mp4_structure_LDADD = ../util/libutil.a

mp4_split_SOURCES = mp4_split.cc $(COMMON_SOURCES)
mp4_split_LDADD = ../util/libutil.a
