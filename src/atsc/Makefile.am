AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util $(libmpeg2_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = decoder

decoder_SOURCES = decoder.cc
decoder_LDADD = ../util/libutil.a $(libmpeg2_LIBS) -lstdc++fs
