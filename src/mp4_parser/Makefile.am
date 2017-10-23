AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = mp4_structure mp4_split

mp4_structure_SOURCES = mp4_structure.cc
mp4_structure_LDADD = ../util/libutil.a

mp4_split_SOURCES = mp4_split.cc
mp4_split_LDADD = ../util/libutil.a
