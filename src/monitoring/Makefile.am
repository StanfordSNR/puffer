AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../net \
	-I$(srcdir)/../notifier $(POSTGRES_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = log_reporter file_reporter

log_reporter_SOURCES = log_reporter.cc influxdb_client.hh influxdb_client.cc \
	../notifier/inotify.hh ../notifier/inotify.cc
log_reporter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs \
	$(POSTGRES_LIBS) $(SSL_LIBS) $(YAML_LIBS)

file_reporter_SOURCES = file_reporter.cc influxdb_client.hh influxdb_client.cc \
	../notifier/inotify.hh ../notifier/inotify.cc
file_reporter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs \
	$(POSTGRES_LIBS) $(SSL_LIBS) $(YAML_LIBS)
