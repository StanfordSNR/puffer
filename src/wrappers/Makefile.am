AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = run_all video_canonicalizer video_encoder video_fragmenter \
	audio_encoder audio_fragmenter ssim_calculator

run_all_SOURCES = run_all.cc yaml.hh yaml.cc
run_all_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS) $(YAML_LIBS)

video_canonicalizer_SOURCES = video_canonicalizer.cc y4m.hh y4m.cc
video_canonicalizer_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

video_encoder_SOURCES = video_encoder.cc
video_encoder_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

video_fragmenter_SOURCES = video_fragmenter.cc
video_fragmenter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

audio_encoder_SOURCES = audio_encoder.cc
audio_encoder_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

audio_fragmenter_SOURCES = audio_fragmenter.cc
audio_fragmenter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)

ssim_calculator_SOURCES = ssim_calculator.cc y4m.hh y4m.cc
ssim_calculator_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs $(SSL_LIBS)
