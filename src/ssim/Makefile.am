AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

bin_PROGRAMS = ssim

ssim_SOURCES = ssim.cc
ssim_LDADD = ../util/libutil.a ../net/libnet.a $(SSL_LIBS)
