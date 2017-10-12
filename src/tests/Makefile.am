AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util

AM_CXXFLAGS = $(PICKY_CXXFLAGS)

LDADD = ../util/libutil.a

check_PROGRAMS = test_mpd

test_mpd_SOURCES = test_mpd.cc

TESTS = $(check_PROGRAMS)
