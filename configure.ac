dnl Process this file with autoconf to produce a configure script.
dnl Copyright (C) 2011 Red Hat, Inc.
dnl See COPYING.LIB for the License of this software

AC_INIT([libstoragemgmt], [1.1.0], [libstoragemgmt-devel@lists.sourceforge.net], [], [http://sourceforge.net/projects/libstoragemgmt/])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
dnl Make automake keep quiet about wildcards & other GNUmake-isms
AM_INIT_AUTOMAKE([-Wno-portability subdir-objects])
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

# Our intention is that we will always be backward compatible.  Thus we will
# set the library version in such a way so that we will always be
# libstoragemgmt.so.1.n.n once we officially release our ver 1.0.0.
#
# To make this happen we will use the minor version as the libtool current and
# age set to minor - 1 and the micro used for revision.  Basically this will get
# us what we expect while utilizing the libtool revision system.
#
# For this to work we need to make sure that when we add to the interface we
# increment minor and set micro to 0.  If we make a code change which doesn't
# change the API we can just bump micro.
#
# 0.1.0 -> libstoragemgmt.so.0.1.0
# 1.0.0 -> libstoragemgmt.so.1.0.0
# 1.1.0 -> libstoragemgmt.so.1.1.0
# 1.1.1 -> libstoragemgmt.so.1.1.1

CURRENT=`expr $LIBSM_MAJOR_VERSION '*' 1 + $LIBSM_MINOR_VERSION`
AGE=$LIBSM_MINOR_VERSION
REVISION=$LIBSM_MICRO_VERSION

LIBSM_LIBTOOL_VERSION=$CURRENT:$REVISION:$AGE

AC_SUBST([LIBSM_MAJOR_VERSION])
AC_SUBST([LIBSM_MINOR_VERSION])
AC_SUBST([LIBSM_MICRO_VERSION])
AC_SUBST([LIBSM_VERSION])
AC_SUBST([LIBSM_VERSION_INFO])
AC_SUBST([LIBSM_VERSION_NUMBER])
AC_SUBST([LIBSM_LIBTOOL_VERSION])

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

AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([int i;])],
   [], [AC_MSG_ERROR([C++ compiler missing or inoperational])])
AC_LANG_POP([C++])

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

#Check for openssl development libs, we are using in simc_lsmplugin
AC_CHECK_LIB([crypto], [MD5_Final], [SSL_LIBS=-lcrypto], AC_MSG_ERROR([Missing openssl-devel libraries]))
AC_SUBST([SSL_LIBS])

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
    [want_c_unit=${enableval}], [])

if test "x${want_c_unit}" = "xyes"; then
    PKG_CHECK_MODULES([LIBCHECK], [check >= 0.9.8 ])
fi

AM_CONDITIONAL([BUILD_C_UNIT], [test "x${want_c_unit}" = "xyes"])

dnl =========================================================================
dnl Check for perl, used for code constants checking
dnl =========================================================================
AC_PATH_PROGS(PERL, perl)

dnl ==========================================================================
dnl Check for python2 as it is needed for the base cmd line function
dnl ==========================================================================

AC_PATH_PROGS(PYTHON, python2.7 python2.6 python, [Python is required])
AC_MSG_CHECKING([Check for Python major version])
PYTHON_MAJOR_VERSION=`$PYTHON -c "import sys; print(sys.version_info[[0]])"`
case "$PYTHON_MAJOR_VERSION" in
    2)
        ;;
    *)
        AC_MSG_ERROR(
            [we need Python version 2.x but found $PYTHON_MAJOR_VERSION.x]) ;;
esac
AC_MSG_RESULT([$PYTHON_MAJOR_VERSION])
AM_PATH_PYTHON([2.6], [], AC_MSG_ERROR([Python interpreter 2.6 or 2.7 required]) )

AC_PYTHON_MODULE([pywbem], [Required])
AC_PYTHON_MODULE([M2Crypto], [Required])
AC_PYTHON_MODULE([argparse], [Required])
AC_PYTHON_MODULE([yaml], [Required])

dnl ==========================================================================
dnl Check for libmicrohttpd and json-c as it is needed for REST API daemon
dnl ==========================================================================

AC_ARG_WITH([rest-api],
    [AS_HELP_STRING([--without-rest-api],
        [Do not build the REST API daemon])],
    [],
    [with_rest_api=yes])
if test "x$with_rest_api" = "xyes"; then
    PKG_CHECK_MODULES([LIBMICROHTTPD], [libmicrohttpd >= 0.9])
    PKG_CHECK_MODULES([JSON], [json >= 0.10], [], [
        PKG_CHECK_MODULES([JSON], [json-c], [], [
            AC_MSG_ERROR([json-c development libraries 0.10 or later required])
        ])
    ])
fi
AM_CONDITIONAL([WITH_REST_API], [test "x$with_rest_api" = "xyes"])

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
          Makefile \
          c_binding/Makefile \
          c_binding/include/Makefile \
          c_binding/include/libstoragemgmt/Makefile \
          c_binding/include/libstoragemgmt/libstoragemgmt_version.h \
          python_binding/Makefile \
          python_binding/lsm/version.py \
          plugin/Makefile \
          plugin/simc/Makefile \
          daemon/Makefile \
          doc/Makefile \
          doc/man/lsmcli.1 \
          doc/man/lsmd.1 \
          doc/doxygen.conf \
          tools/Makefile \
          tools/udev/Makefile \
          tools/lsmcli/Makefile \
          tools/utility/Makefile \
          packaging/Makefile \
          packaging/daemon/Makefile \
          packaging/libstoragemgmt.spec \
          doc/man/Makefile \
          test/Makefile)
