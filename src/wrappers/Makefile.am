AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util \
	-isystem$(srcdir)/../../third_party/yaml-cpp.upstream/include
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = run_all

run_all_SOURCES = run_all.cc
run_all_LDADD = ../util/libutil.a \
	../../third_party/yaml-cpp/libyaml-cpp.a -lstdc++fs
