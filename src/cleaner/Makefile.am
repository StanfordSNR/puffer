AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = cleaner depcleaner windowcleaner

cleaner_SOURCES = cleaner.cc
cleaner_LDADD = ../util/libutil.a -lstdc++fs

depcleaner_SOURCES = depcleaner.cc
depcleaner_LDADD = ../util/libutil.a -lstdc++fs

windowcleaner_SOURCES = windowcleaner.cc
windowcleaner_LDADD = ../util/libutil.a -lstdc++fs
