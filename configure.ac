#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([puffer], [0.2], [fyy@cs.stanford.edu])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_HEADERS([config.h])

# Add picky CXXFLAGS
CXX17_FLAGS="-std=c++17 -pthread"
PICKY_CXXFLAGS="-pedantic -Wall -Wextra -Weffc++ -Werror"
AC_SUBST([CXX17_FLAGS])
AC_SUBST([PICKY_CXXFLAGS])

# Checks for programs.
AC_PROG_CXX
AC_PROG_RANLIB

# Checks for libraries.
PKG_CHECK_MODULES([libmpeg2],[libmpeg2])
PKG_CHECK_MODULES([opus],[opus])
PKG_CHECK_MODULES([sndfile],[sndfile])
PKG_CHECK_MODULES([libavformat],[libavformat])
PKG_CHECK_MODULES([libavutil],[libavutil])
PKG_CHECK_MODULES([POSTGRES],[libpqxx libpq])
PKG_CHECK_MODULES([YAML],[yaml-cpp])
PKG_CHECK_MODULES([SSL],[libssl libcrypto])
PKG_CHECK_MODULES([CRYPTO],[libcrypto++])

# Checks for header files.
AC_LANG_PUSH(C++)
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CXX17_FLAGS"
AC_CHECK_HEADERS([string_view])
AC_CHECK_HEADERS([experimental/string_view])
#AC_CHECK_HEADERS([filesystem])
AC_CHECK_HEADERS([experimental/filesystem])
CPPFLAGS="$save_CPPFLAGS"
AC_LANG_POP(C++)

AC_ARG_ENABLE([sanitize],
  [AS_HELP_STRING([--enable-sanitize],
     [build with address and undefined-behavior santizers])],
  [EXTRA_CXXFLAGS="-fsanitize=address -fsanitize=undefined -fuse-ld=gold"],
  [sanitize=false])

AC_SUBST(EXTRA_CXXFLAGS)

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_SEARCH_LIBS([a52_init], [a52], , [AC_MSG_ERROR([Unable to find liba52.])])

AC_CONFIG_FILES([
    Makefile
    third_party/Makefile
    src/Makefile
    src/util/Makefile
    src/net/Makefile
    src/notifier/Makefile
    src/atsc/Makefile
    src/forwarder/Makefile
    src/mp4/Makefile
    src/webm/Makefile
    src/mpd/Makefile
    src/ssim/Makefile
    src/cleaner/Makefile
    src/time/Makefile
    src/monitoring/Makefile
    src/wrappers/Makefile
    src/opus-encoder/Makefile
    src/media-server/Makefile
    src/tests/Makefile
])
AC_OUTPUT
