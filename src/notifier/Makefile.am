AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

COMMON_SOURCES = notifier.hh notifier.cc

bin_PROGRAMS = notify_demo
notify_demo_SOURCES = notify_demo.cc $(COMMON_SOURCES)
notify_demo_LDADD = ../util/libutil.a
