AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../net
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = udp_to_tcp

udp_to_tcp_SOURCES = udp_to_tcp.cc
udp_to_tcp_LDADD = ../util/libutil.a ../net/libnet.a $(SSL_LIBS)
