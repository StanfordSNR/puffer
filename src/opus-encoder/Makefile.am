AM_CPPFLAGS = $(CXX17_FLAGS) $(opus_CFLAGS) $(sndfile_CFLAGS) \
	$(libavformat_CFLAGS) $(libavutil_CFLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = opus-encoder

opus_encoder_SOURCES = opus-encoder.cc
opus_encoder_LDADD = ../util/libutil.a $(opus_LIBS) \
	$(sndfile_LIBS) $(libavformat_LIBS) $(libavutil_LIBS)
