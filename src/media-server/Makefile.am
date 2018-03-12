AM_CPPFLAGS = -I$(srcdir)/../util -I$(srcdir)/../net $(CXX14_FLAGS) $(SSL_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = ws_media_server

ws_media_server_SOURCES = ws_media_server.cc
ws_media_server_LDADD = ../net/libnet.a ../util/libutil.a ../net/libnet.a $(SSL_LIBS) $(CRYPTO_LIBS) -lstdc++fs
