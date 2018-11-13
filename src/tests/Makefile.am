AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

LDADD = ../util/libutil.a

AM_TESTS_ENVIRONMENT = \
	export abs_srcdir=$(abs_srcdir); \
	export abs_builddir=$(abs_builddir); \
	export test_tmpdir=$(abs_builddir)/test_tmpdir; \
	mkdir -p $$test_tmpdir;

EXTRA_DIST = test_helpers.py

dist_check_SCRIPTS = fetch_vectors.test udp_to_tcp.test notify_good_prog.test \
	notify_bad_prog.test cleaner.test ssim.test mpd.test time.test cleanup.test \
	mp4.test depcleaner.test windowcleaner.test

TESTS = $(check_PROGRAMS) $(dist_check_SCRIPTS)

clean-local:
	-rm -rf $(abs_builddir)/test_tmpdir
	-rm -rf $(abs_builddir)/test-vectors

mpd.log: fetch_vectors.log

ssim.log: fetch_vectors.log

mp4.log: fetch_vectors.log

cleanup.log: mpd.log ssim.log mp4.log
