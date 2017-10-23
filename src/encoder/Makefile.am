AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

bin_PROGRAMS = encoder ts_segmentation

encoder_SOURCES = encoder.cc
encoder_LDADD = ../util/libutil.a

ts_segmentation_SOURCES = ts_segmentation.cc
ts_segmentation_LDADD = ../util/libutil.a
