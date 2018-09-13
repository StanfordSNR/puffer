AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../notifier \
  -I$(srcdir)/../net $(POSTGRES_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = log_reporter

log_reporter_SOURCES = log_reporter.cc \
	../notifier/inotify.hh ../notifier/inotify.cc
log_reporter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs \
	$(POSTGRES_LIBS) $(SSL_LIBS)
