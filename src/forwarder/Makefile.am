AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = forwarder
forwarder_SOURCES = forwarder.cc
forwarder_LDADD = ../util/libutil.a
forwarder_LDFLAGS = -pthread
