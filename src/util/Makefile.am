AM_CPPFLAGS = -I$(srcdir)/../net $(CXX17_FLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

noinst_LIBRARIES = libutil.a

libutil_a_SOURCES = \
	child_process.hh child_process.cc \
	exception.hh \
	file_descriptor.hh file_descriptor.cc \
	path.hh path.cc \
	pipe.hh pipe.cc \
	poller.hh poller.cc \
	signalfd.hh signalfd.cc \
	strict_conversions.hh strict_conversions.cc \
	system_runner.hh system_runner.cc \
	temp_dir.hh temp_dir.cc \
	temp_file.hh temp_file.cc \
	timeit.hh timeit.cc \
	timestamp.hh timestamp.cc \
	timerfd.hh timerfd.cc \
	tokenize.hh tokenize.cc \
	formatter.hh formatter.cc \
	util.hh util.cc \
	filesystem.hh \
	chunk.hh \
	mmap.hh mmap.cc \
	y4m.hh y4m.cc \
	ipc_socket.hh ipc_socket.cc \
	pid.hh pid.cc \
	media_formats.hh media_formats.cc \
	yaml.hh yaml.cc
