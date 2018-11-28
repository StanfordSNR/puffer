AM_CPPFLAGS = $(CXX17_FLAGS) $(SSL_CFLAGS)  $(POSTGRES_CFLAGS) \
	-I$(srcdir)/../util -I$(srcdir)/../net -I$(srcdir)/../notifier \
	-I$(srcdir)/../monitoring -I$(srcdir)/../abr \
	-isystem$(srcdir)/../../third_party/json.upstream/single_include/nlohmann
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = ws_media_server run_servers

ws_media_server_SOURCES = ws_media_server.cc \
	ws_client.hh ws_client.cc channel.hh channel.cc \
	media_formats.hh media_formats.cc yaml.hh yaml.cc \
	client_message.hh client_message.cc server_message.hh server_message.cc \
	../notifier/inotify.hh ../notifier/inotify.cc \
	../abr/abr_algo.hh ../abr/linear_bba.hh ../abr/linear_bba.cc \
	../abr/mpc.hh ../abr/mpc.cc \
	../../third_party/json.upstream/single_include/nlohmann/json.hpp
ws_media_server_LDADD = ../util/libutil.a ../net/libnet.a ../util/libutil.a \
	$(POSTGRES_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(YAML_LIBS) -lstdc++fs

run_servers_SOURCES = run_servers.cc \
	media_formats.hh media_formats.cc yaml.hh yaml.cc \
	../monitoring/influxdb_client.hh ../monitoring/influxdb_client.cc
run_servers_LDADD = ../util/libutil.a ../net/libnet.a \
	$(POSTGRES_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(YAML_LIBS) -lstdc++fs
