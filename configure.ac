#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([tv-encoder], [0.1], [fyy@cs.stanford.edu])
AM_INIT_AUTOMAKE([foreign])
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

# Checks for header files.
AC_LANG_PUSH(C++)
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CXX17_FLAGS"
AC_CHECK_HEADERS([string_view])
AC_CHECK_HEADERS([experimental/string_view])
CPPFLAGS="$save_CPPFLAGS"
AC_LANG_POP(C++)

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([
    Makefile
    third_party/Makefile
    src/Makefile
    src/util/Makefile
    src/notifier/Makefile
    src/atsc/Makefile
    src/forwarder/Makefile
    src/encoder/Makefile
    src/mp4/Makefile
    src/ssim/Makefile
    src/cleaner/Makefile
    src/tests/Makefile
])
AC_OUTPUT
