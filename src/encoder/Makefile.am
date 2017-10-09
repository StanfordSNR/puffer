AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = encoder
encoder_SOURCES = encoder.cc
encoder_LDADD = ../util/libutil.a
encoder_LDFLAGS = -pthread
