dnl Process this file with autoconf to produce a configure script.
dnl Copyright (C) 2011 Red Hat, Inc.
dnl See COPYING.LIB for the License of this software

AC_INIT([libstoragemgmt], [0.0.20], [libstoragemgmt-devel@lists.sourceforge.net], [], [http://sourceforge.net/projects/libstoragemgmt/])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
dnl Make automake keep quiet about wildcards & other GNUmake-isms
AM_INIT_AUTOMAKE([-Wno-portability])
AM_MAINTAINER_MODE([enable])
# Enable silent build when available (Automake 1.11)
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])
PKG_PROG_PKG_CONFIG

AC_CANONICAL_HOST

LIBSM_MAJOR_VERSION=`echo $VERSION | awk -F. '{print $1}'`
LIBSM_MINOR_VERSION=`echo $VERSION | awk -F. '{print $2}'`
LIBSM_MICRO_VERSION=`echo $VERSION | awk -F. '{print $3}'`
LIBSM_VERSION=$LIBSM_MAJOR_VERSION.$LIBSM_MINOR_VERSION.$LIBSM_MICRO_VERSION$LIBSM_MICRO_VERSION_SUFFIX
LIBSM_VERSION_INFO=`expr $LIBSM_MAJOR_VERSION + $LIBSM_MINOR_VERSION`:$LIBSM_MICRO_VERSION:$LIBSM_MINOR_VERSION
LIBSM_VERSION_NUMBER=`expr $LIBSM_MAJOR_VERSION \* 1000000 + $LIBSM_MINOR_VERSION \* 1000 + $LIBSM_MICRO_VERSION`

AC_SUBST([LIBSM_MAJOR_VERSION])
AC_SUBST([LIBSM_MINOR_VERSION])
AC_SUBST([LIBSM_MICRO_VERSION])
AC_SUBST([LIBSM_VERSION])
AC_SUBST([LIBSM_VERSION_INFO])
AC_SUBST([LIBSM_VERSION_NUMBER])

dnl Required minimum versions of all libs we depend on
LIBXML_REQUIRED="2.5.0"

dnl Checks for C compiler.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_CPP
AM_PROG_CC_STDC

AM_PROG_LIBTOOL
AM_PROG_CC_C_O
AM_PROG_LD

AC_CHECK_HEADERS([stdint.h stdlib.h string.h sys/socket.h syslog.h unistd.h])

#Make sure all types are covered
AC_HEADER_STDBOOL
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([getpass memset socket strchr strdup strtol strtoul])

dnl Availability of various common headers (non-fatal if missing).
AC_CHECK_HEADERS([dlfcn.h])

#Check for json parser yajl
AC_CHECK_HEADERS([yajl/yajl_gen.h yajl/yajl_parse.h], [] , AC_MSG_ERROR([Missing yajl development headers]) )
AC_CHECK_HEADERS([yajl/yajl_version.h])
AC_CHECK_LIB([yajl], [yajl_parse], [YAJL_LIBS=-lyajl], AC_MSG_ERROR([Missing yajl library]))
AC_SUBST([YAJL_LIBS])

dnl if --prefix is /usr, don't use /usr/var for localstatedir
dnl or /usr/etc for sysconfdir
dnl as this makes a lot of things break in testing situations

if test "$prefix" = "/usr" && test "$localstatedir" = '${prefix}/var' ; then
    localstatedir='/var'
fi
if test "$prefix" = "/usr" && test "$sysconfdir" = '${prefix}/etc' ; then
    sysconfdir='/etc'
fi

dnl ==========================================================================
dnl find libxml2 library, borrowed from xmlsec
dnl ==========================================================================

PKG_CHECK_MODULES([LIBXML], [libxml-2.0])
PKG_CHECK_MODULES([LIBGLIB], [glib-2.0 >= 2.22.5])

want_c_unit="yes"
AC_ARG_ENABLE([build-c-unit],
    [AC_HELP_STRING([--disable-build-c-unit], [disable building C unit test case.])],
    [want_c_unit=$enableval], [])

if test "x${want_c_unit}" = "xyes"; then
    PKG_CHECK_MODULES([LIBCHECK], [check >= 0.9.8 ])
fi

AM_CONDITIONAL([BUILD_C_UNIT], [test "x${want_c_unit}" = "xyes"])

dnl ==========================================================================
dnl Check for python as it is needed for the daemon
dnl ==========================================================================

AM_PATH_PYTHON([2.6], [], AC_MSG_ERROR([Python interpreter 2.6 or later required]) )
AC_PYTHON_MODULE([pywbem], [Required])
AC_PYTHON_MODULE([M2Crypto], [Required])

#Setup the unit directory for systemd stuff
PKG_PROG_PKG_CONFIG
AC_ARG_WITH([systemdsystemunitdir],
        AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
        [], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != xno; then
        AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != xno ])

AC_OUTPUT(libstoragemgmt.pc \
          libstoragemgmt.spec \
          Makefile \
          src/Makefile \
          plugin/Makefile \
          doc/Makefile \
          doc/man/lsmcli.1 \
          doc/man/lsmd.1 \
          doc/doxygen.conf \
          tools/Makefile \
          tools/lsmcli/Makefile \
          include/Makefile \
          include/libstoragemgmt/Makefile \
          include/libstoragemgmt/libstoragemgmt_version.h \
          lsm/Makefile \
          lsm/lsm/version.py \
          tools/lsmclipy/Makefile \
          packaging/Makefile \
          packaging/daemon/Makefile \
          doc/man/Makefile \
          test/Makefile)
