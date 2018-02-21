AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = cleaner rawcleaner

cleaner_SOURCES = cleaner.cc
cleaner_LDADD = ../util/libutil.a -lstdc++fs

rawcleaner_SOURCES = rawcleaner.cc
rawcleaner_LDADD = ../util/libutil.a -lstdc++fs
