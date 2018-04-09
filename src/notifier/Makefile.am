AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../net
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = notifier

notifier_SOURCES = notifier.hh notifier.cc inotify.hh inotify.cc
notifier_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)
