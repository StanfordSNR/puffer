AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util \
	-isystem$(srcdir)/../../third_party/libwebm.upstream/webm_parser/include \
	-isystem$(srcdir)/../../third_party/libwebm.upstream
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

noinst_LIBRARIES = libwebm.a

libwebm_a_SOURCES = \
	webm_info.hh webm_info.cc

bin_PROGRAMS = webm_fragment webm_probe

webm_fragment_SOURCES = webm_fragment.cc
webm_fragment_LDADD = ../util/libutil.a ../../third_party/libwebm/libwebm.a

webm_probe_SOURCES = webm_probe.cc
webm_probe_LDADD = libwebm.a ../util/libutil.a
