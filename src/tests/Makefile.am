AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util

AM_CXXFLAGS = $(PICKY_CXXFLAGS)

LDADD = ../util/libutil.a

AM_TESTS_ENVIRONMENT = \
  export abs_srcdir=$(abs_srcdir); \
  export abs_builddir=$(abs_builddir); \
  export test_tmpdir=$(abs_builddir)/test_tmpdir; \
  mkdir -p $$test_tmpdir;

EXTRA_DIST = test_helpers.py notified_good_prog.sh notified_bad_prog.sh

dist_check_SCRIPTS = fetch-vectors.test test_notifier.py forwarder.test \
					 cleaner.test ssim.test mpd.test mp4.test cleanup.test

TESTS = $(check_PROGRAMS) $(dist_check_SCRIPTS)

clean-local:
	-rm -rf $(abs_builddir)/test_tmpdir
	-rm -rf $(abs_builddir)/test-vectors

mpd.log: fetch-vectors.log

ssim.log: fetch-vectors.log

mp4.log: fetch-vectors.log

cleanup.log: mpd.log ssim.log mp4.log
