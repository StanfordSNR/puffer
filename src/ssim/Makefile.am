AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../mp4
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = ssim

ssim_SOURCES = ssim.cc
ssim_LDADD = ../util/libutil.a ../mp4/libmp4.a
