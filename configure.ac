#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([tv-encoder], [0.1], [fyy@cs.stanford.edu])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_HEADERS([config.h])

# Add picky CXXFLAGS
CXX14_FLAGS="-std=c++14 -pthread"
PICKY_CXXFLAGS="-pedantic -Wall -Wextra -Weffc++ -Werror"
AC_SUBST([CXX14_FLAGS])
AC_SUBST([PICKY_CXXFLAGS])

# Checks for programs.
AC_PROG_CXX
AC_PROG_RANLIB

# Checks for libraries.

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile
    src/Makefile
    src/util/Makefile
    src/encoder/Makefile
    src/forwarder/Makefile
    src/tests/Makefile])
AC_OUTPUT
