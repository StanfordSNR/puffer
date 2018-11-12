AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../net
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = udp_to_tcp file_receiver file_sender

udp_to_tcp_SOURCES = udp_to_tcp.cc
udp_to_tcp_LDADD = ../util/libutil.a ../net/libnet.a $(SSL_LIBS)

file_receiver_SOURCES = file_receiver.cc file_message.hh file_message.cc
file_receiver_LDADD = ../util/libutil.a ../net/libnet.a $(SSL_LIBS) -lstdc++fs

file_sender_SOURCES = file_sender.cc file_message.hh file_message.cc
file_sender_LDADD = ../util/libutil.a ../net/libnet.a $(SSL_LIBS) -lstdc++fs
