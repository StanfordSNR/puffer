AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

noinst_LIBRARIES = libmp4.a

libmp4_a_SOURCES = \
	mp4_file.hh mp4_file.cc \
	box.hh box.cc \
	mvhd_box.hh mvhd_box.cc \
	mfhd_box.hh mfhd_box.cc \
	tfhd_box.hh tfhd_box.cc \
	trex_box.hh trex_box.cc \
	sidx_box.hh sidx_box.cc \
	stsd_box.hh stsd_box.cc \
	mp4_parser.hh mp4_parser.cc \
	mp4_info.hh mp4_info.cc

bin_PROGRAMS = mp4_structure mp4_split

mp4_structure_SOURCES = mp4_structure.cc
mp4_structure_LDADD = ../util/libutil.a libmp4.a

mp4_split_SOURCES = mp4_split.cc
mp4_split_LDADD = ../util/libutil.a libmp4.a
