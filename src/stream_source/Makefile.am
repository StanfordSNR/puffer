AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = stream_source
stream_source_SOURCES = stream_source.cc
stream_source_LDADD = ../util/libutil.a
stream_source_LDFLAGS = -pthread
