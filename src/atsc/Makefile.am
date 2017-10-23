AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util $(libmpeg2_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = mpeg2demo

mpeg2demo_SOURCES = mpeg2demo.cc
mpeg2demo_LDADD = ../util/libutil.a $(libmpeg2_LIBS)
