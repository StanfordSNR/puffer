AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util \
	-isystem$(srcdir)/../../third_party/yaml-cpp.upstream/include
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = run_all canonicalizer video_encoder video_fragment \
	audio_encoder audio_fragment ssim_calculator

run_all_SOURCES = run_all.cc
run_all_LDADD = ../util/libutil.a \
	../../third_party/yaml-cpp/libyaml-cpp.a -lstdc++fs

canonicalizer_SOURCES = canonicalizer.cc
canonicalizer_LDADD = ../util/libutil.a -lstdc++fs

video_encoder_SOURCES = video_encoder.cc
video_encoder_LDADD = ../util/libutil.a -lstdc++fs

video_fragment_SOURCES = video_fragment.cc
video_fragment_LDADD = ../util/libutil.a -lstdc++fs

audio_encoder_SOURCES = audio_encoder.cc
audio_encoder_LDADD = ../util/libutil.a -lstdc++fs

audio_fragment_SOURCES = audio_fragment.cc
audio_fragment_LDADD = ../util/libutil.a -lstdc++fs

ssim_calculator_SOURCES = ssim_calculator.cc
ssim_calculator_LDADD = ../util/libutil.a -lstdc++fs
