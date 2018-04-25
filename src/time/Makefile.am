AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = time

time_SOURCES = time.cc

time_LDADD = ../util/libutil.a -lstdc++fs
