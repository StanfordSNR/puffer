AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

COMMON_SOURCES = notifier.hh notifier.cc

bin_PROGRAMS = run_notifier
run_notifier_SOURCES = run_notifier.cc $(COMMON_SOURCES)
run_notifier_LDADD = ../util/libutil.a
