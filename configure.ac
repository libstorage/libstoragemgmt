dnl Process this file with autoconf to produce a configure script.
dnl Copyright (C) 2011 Red Hat, Inc.
dnl See COPYING.LIB for the License of this software

AC_INIT([libstoragemgmt], [0.0.8], [libstoragemgmt-devel@lists.sourceforge.net], [], [http://sourceforge.net/apps/trac/libstoragemgmt/])
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

dnl Make sure we have an ANSI compiler
AM_C_PROTOTYPES
test "x$U" != "x" && AC_MSG_ERROR([Compiler not ANSI compliant])

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

dnl ==========================================================================
dnl Check whether to build SMI-S C++ plugin
dnl ==========================================================================

want_smis_cpp="yes"
AC_ARG_ENABLE([smis-cpp],
    [AC_HELP_STRING([--disable-smis-cpp], [disable SMI-S C++ plugin build.])],
    [want_smis_cpp=$enableval], [])

if test "x${want_smis_cpp}" = "xyes"; then

    # this sucks but Pegasus requires this test per system, arch, compiler
    PEGASUS_CFLAGS=
    case `uname` in
        *Linux*) case `uname -m` in
                     x86_64) PEGASUS_CFLAGS="-DPEGASUS_PLATFORM_LINUX_X86_64_GNU";;
                     i?86)   PEGASUS_CFLAGS="-DPEGASUS_PLATFORM_LINUX_IX86_GNU";;
                 esac
    esac
    if test "$PEGASUS_CFLAGS" = "" ; then
        AC_MSG_ERROR([Unsupported platform, check see configure.ac \
                      and Pegasus/Common/Config.h])
    fi
    saved_cflags=$CFLAGS
    CFLAGS="$CFLAGS $PEGASUS_CFLAGS"
    AC_CHECK_HEADER(Pegasus/Common/Config.h,,
                    AC_MSG_ERROR([Pegasus development headers are missing]))

    # look for pegcommon and pegclient libraries, they are C++ and this
    # is a problem with automake, don't look for symbols !
    saved_ldflags=$LDFLAGS
    PEGASUS_LIBS=
    AC_MSG_CHECKING([for Pegasus libraries])
    LDFLAGS="${LDFLAGS} -lpegcommon -lpegclient"
    AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <Pegasus/Common/Config.h>],
                                    [])],
                   [AC_MSG_RESULT(found)]
                   [PEGASUS_LIBS="-lpegcommon -lpegclient"],
                   [AC_MSG_ERROR([Pegasus libraries are missing])])

    LDFLAGS=$saved_ldflags
    CFLAGS=$saved_cflags
fi

AC_SUBST(PEGASUS_CFLAGS)
AC_SUBST(PEGASUS_LIBS)

AM_CONDITIONAL([BUILD_SMIS_CPP], [test "x${want_smis_cpp}" = "xyes"])

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
          tools/smiscli/Makefile \
          include/Makefile \
          include/libstoragemgmt/Makefile \
          lsm/Makefile \
          lsm/lsm/version.py \
          tools/lsmclipy/Makefile \
          packaging/Makefile \
          packaging/daemon/Makefile \
          doc/man/Makefile \
          test/Makefile)
