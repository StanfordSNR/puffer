AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = cleaner depcleaner

cleaner_SOURCES = cleaner.cc
cleaner_LDADD = ../util/libutil.a -lstdc++fs

depcleaner_SOURCES = depcleaner.cc
depcleaner_LDADD = ../util/libutil.a -lstdc++fs
