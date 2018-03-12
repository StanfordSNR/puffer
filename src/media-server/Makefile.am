AM_CPPFLAGS = $(CXX17_FLAGS) $(SSL_CFLAGS) \
	-I$(srcdir)/../util -I$(srcdir)/../net -I$(srcdir)/../notifier \
	-isystem$(srcdir)/../../third_party/yaml-cpp.upstream/include
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = ws_media_server

ws_media_server_SOURCES = ws_media_server.cc \
	../notifier/inotify.hh ../notifier/inotify.cc ws_client.hh ws_client.cc
ws_media_server_LDADD = ../net/libnet.a ../util/libutil.a ../net/libnet.a \
	$(SSL_LIBS) $(CRYPTO_LIBS) -lstdc++fs ../../third_party/yaml-cpp/libyaml-cpp.a
