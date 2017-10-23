AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util

AM_CXXFLAGS = $(PICKY_CXXFLAGS)

LDADD = ../util/libutil.a

AM_TESTS_ENVIRONMENT = \
  export abs_srcdir=$(abs_srcdir); \
  export abs_builddir=$(abs_builddir); \
  export test_tmpdir=$(abs_builddir)/test_tmpdir; \
  mkdir -p $$test_tmpdir;

check_PROGRAMS = test_mpd

test_mpd_SOURCES = test_mpd.cc

EXTRA_DIST = test_helpers.py notified_good_prog.sh notified_bad_prog.sh

dist_check_SCRIPTS = test_notifier.py test_forwarder.py test_cleaner.py

TESTS = $(check_PROGRAMS) $(dist_check_SCRIPTS)

clean-local:
	-rm -rf $(abs_builddir)/test_tmpdir
