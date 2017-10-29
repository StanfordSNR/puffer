AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = mpd_writer

mpd_writer_SOURCES = mpd_writer.cc mpd.hh mpd.cc

mpd_writer_LDADD = ../util/libutil.a
