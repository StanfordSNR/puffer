AM_CPPFLAGS = $(CXX17_FLAGS) $(SSL_CFLAGS) $(POSTGRES_CFLAGS) \
	-I$(srcdir)/../util -I$(srcdir)/../net -I$(srcdir)/../notifier \
	-I$(srcdir)/../monitoring -I$(srcdir)/../abr \
	-isystem$(srcdir)/../../third_party/json.upstream/single_include/nlohmann \
	-isystem$(srcdir)/../../third_party/libtorch/include
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = run_servers maintenance_server ws_media_server

ws_media_server_SOURCES = ws_media_server.cc \
	ws_client.hh ws_client.cc channel.hh channel.cc \
	client_message.hh client_message.cc server_message.hh server_message.cc \
	../notifier/inotify.hh ../notifier/inotify.cc \
	../abr/abr_algo.hh ../abr/abr_algo.cc \
	../abr/linear_bba.hh ../abr/linear_bba.cc \
	../abr/mpc.hh ../abr/mpc.cc \
	../abr/mpc_search.hh ../abr/mpc_search.cc \
	../abr/pensieve.hh ../abr/pensieve.cc \
	../abr/puffer.hh ../abr/puffer.cc \
	../abr/puffer_raw.hh ../abr/puffer_raw.cc \
	../abr/puffer_ttp.cc ../abr/puffer_ttp.hh \
	../../third_party/json.upstream/single_include/nlohmann/json.hpp
ws_media_server_LDFLAGS = -L../../third_party/libtorch/lib \
	'-Wl,-rpath,$$ORIGIN/../../third_party/libtorch/lib'
ws_media_server_LDADD = ../util/libutil.a ../net/libnet.a ../util/libutil.a \
	$(POSTGRES_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(YAML_LIBS) -lstdc++fs \
	-ltorch -lcaffe2 -lc10 -lmkldnn

run_servers_SOURCES = run_servers.cc
	../monitoring/influxdb_client.hh ../monitoring/influxdb_client.cc
run_servers_LDADD = ../util/libutil.a ../net/libnet.a \
	$(POSTGRES_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(YAML_LIBS) -lstdc++fs

maintenance_server_SOURCES = maintenance_server.cc \
	server_message.hh server_message.cc
maintenance_server_LDADD = ../util/libutil.a ../net/libnet.a ../util/libutil.a \
	$(SSL_LIBS) $(CRYPTO_LIBS) $(YAML_LIBS)
