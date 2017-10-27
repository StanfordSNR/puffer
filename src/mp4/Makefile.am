AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

noinst_LIBRARIES = libmp4.a

libmp4_a_SOURCES = mp4.hh mp4.cc

bin_PROGRAMS = mp4_structure mp4_split

mp4_structure_SOURCES = mp4_structure.cc
mp4_structure_LDADD = ../util/libutil.a libmp4.a

mp4_split_SOURCES = mp4_split.cc
mp4_split_LDADD = ../util/libutil.a libmp4.a
