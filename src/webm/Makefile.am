AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util \
	-I$(srcdir)/../../third_party/libwebm.upstream/webm_parser/include
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = webm_fragment

webm_fragment_SOURCES = webm_fragment.cc
webm_fragment_LDADD = ../util/libutil.a ../../third_party/libwebm/libwebm.a
