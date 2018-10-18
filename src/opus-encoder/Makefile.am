AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util $(opus_CFLAGS) $(sndfile_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = opus-encoder

opus_encoder_SOURCES = opus-encoder.cc
opus_encoder_LDADD = ../util/libutil.a $(opus_LIBS) $(sndfile_LIBS)
