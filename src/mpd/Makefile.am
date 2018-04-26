AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../mp4 \
			  -I$(srcdir)/../webm \
			  -I$(srcdir)/../../third_party/libwebm.upstream/webm_parser/include
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = mpd_writer

mpd_writer_SOURCES = mpd_writer.cc mpd.hh mpd.cc
mpd_writer_LDADD = ../util/libutil.a ../mp4/libmp4.a ../webm/libwebm.a -lstdc++fs
