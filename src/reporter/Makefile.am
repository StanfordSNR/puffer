
# Use c++11 here to fix an error with pqxx in c++17.
# error: ISO C++1z does not allow dynamic exception specifications
CXX11_FLAGS =-std=c++11

AM_CPPFLAGS = $(CXX11_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = file_reporter ssim_reporter

file_reporter_SOURCES = file_reporter.cc
file_reporter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs -lpqxx -lpq $(SSL_LIBS)

ssim_reporter_SOURCES = ssim_reporter.cc
ssim_reporter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs -lpqxx -lpq $(SSL_LIBS)
