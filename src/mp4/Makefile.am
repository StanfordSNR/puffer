AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

noinst_LIBRARIES = libmp4.a

libmp4_a_SOURCES = \
	mp4_file.hh mp4_file.cc \
	box.hh box.cc \
	ftyp_box.hh ftyp_box.cc \
	mvhd_box.hh mvhd_box.cc \
	mfhd_box.hh mfhd_box.cc \
	trex_box.hh trex_box.cc \
	sidx_box.hh sidx_box.cc \
	stco_box.hh stco_box.cc \
	stsc_box.hh stsc_box.cc \
	stsd_box.hh stsd_box.cc \
	stss_box.hh stss_box.cc \
	stsz_box.hh stsz_box.cc \
	stts_box.hh stts_box.cc \
	trun_box.hh trun_box.cc \
	tfhd_box.hh tfhd_box.cc \
	tkhd_box.hh tkhd_box.cc \
	mdhd_box.hh mdhd_box.cc \
	tfdt_box.hh tfdt_box.cc \
	elst_box.hh elst_box.cc \
	ctts_box.hh ctts_box.cc \
	mp4_parser.hh mp4_parser.cc \
	mp4_info.hh mp4_info.cc

bin_PROGRAMS = mp4_structure mp4_fragment

mp4_structure_SOURCES = mp4_structure.cc
mp4_structure_LDADD = libmp4.a ../util/libutil.a

mp4_fragment_SOURCES = mp4_fragment.cc
mp4_fragment_LDADD = libmp4.a ../util/libutil.a -lstdc++fs
