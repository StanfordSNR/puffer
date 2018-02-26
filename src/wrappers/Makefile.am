AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util \
	-isystem$(srcdir)/../../third_party/yaml-cpp.upstream/include
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = run_all video_canonicalizer video_encoder video_fragmenter \
	audio_encoder audio_fragmenter ssim_calculator

run_all_SOURCES = run_all.cc
run_all_LDADD = ../util/libutil.a \
	../../third_party/yaml-cpp/libyaml-cpp.a -lstdc++fs

video_canonicalizer_SOURCES = video_canonicalizer.cc
video_canonicalizer_LDADD = ../util/libutil.a -lstdc++fs

video_encoder_SOURCES = video_encoder.cc
video_encoder_LDADD = ../util/libutil.a -lstdc++fs

video_fragmenter_SOURCES = video_fragmenter.cc
video_fragmenter_LDADD = ../util/libutil.a -lstdc++fs

audio_encoder_SOURCES = audio_encoder.cc
audio_encoder_LDADD = ../util/libutil.a -lstdc++fs

audio_fragmenter_SOURCES = audio_fragmenter.cc
audio_fragmenter_LDADD = ../util/libutil.a -lstdc++fs

ssim_calculator_SOURCES = ssim_calculator.cc
ssim_calculator_LDADD = ../util/libutil.a -lstdc++fs
