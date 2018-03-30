# Use c++14 here to fix an error with pqxx in c++17.
# error: ISO C++1z does not allow dynamic exception specifications
CXX14_FLAGS="-std=c++14 -pthread"

AM_CPPFLAGS = $(CXX14_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = file_reporter ssim_reporter

file_reporter_SOURCES = file_reporter.cc
file_reporter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs \
	$(POSTGRES_LIBS) $(SSL_LIBS)

ssim_reporter_SOURCES = ssim_reporter.cc
ssim_reporter_LDADD = ../util/libutil.a ../net/libnet.a -lstdc++fs \
	$(POSTGRES_LIBS) $(SSL_LIBS)
