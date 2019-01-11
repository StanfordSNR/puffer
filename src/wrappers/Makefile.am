AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = video_canonicalizer video_encoder video_fragmenter \
	audio_fragmenter ssim_calculator generate_mpd run_pipeline

video_canonicalizer_SOURCES = video_canonicalizer.cc
video_canonicalizer_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

video_encoder_SOURCES = video_encoder.cc
video_encoder_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

video_fragmenter_SOURCES = video_fragmenter.cc
video_fragmenter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

audio_fragmenter_SOURCES = audio_fragmenter.cc
audio_fragmenter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

ssim_calculator_SOURCES = ssim_calculator.cc
ssim_calculator_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

generate_mpd_SOURCES = generate_mpd.cc
generate_mpd_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

run_pipeline_SOURCES = run_pipeline.cc
run_pipeline_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS) $(YAML_LIBS)
