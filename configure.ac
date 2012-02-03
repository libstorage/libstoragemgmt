dnl Process this file with autoconf to produce a configure script.
dnl Copyright (C) 2011 Red Hat, Inc.
dnl See COPYING.LIB for the License of this software

AC_INIT([libstoragemgmt], [0.0.1alpha], [libstoragemgmt-devel@lists.sourceforge.net], [], [http://sourceforge.net/apps/trac/libstoragemgmt/])
AC_CONFIG_SRCDIR([src/lsm_datatypes.cpp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
dnl Make automake keep quiet about wildcards & other GNUmake-isms
AM_INIT_AUTOMAKE([-Wno-portability])
AM_MAINTAINER_MODE([enable])

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
YAJL_FOUND=no
AC_CHECK_HEADERS([yajl/yajl_gen.h yajl/yajl_parse.h], [] , AC_MSG_ERROR([Missing yajl development headers]) )

#OK is there a better way to do this?
YAJL_LIBS="-lyajl"
AC_SUBST([YAJL_LIBS])

#Check for boost specific headers
AC_CHECK_HEADERS([boost/regex.hpp], [] , AC_MSG_ERROR([boost headers missing and are required]) )
BOOST_LIBS="-lboost_regex"
AC_SUBST([BOOST_LIBS])

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
LIBXML_CONFIG="xml2-config"
LIBXML_CFLAGS=""
LIBXML_LIBS=""
LIBXML_FOUND="no"

AC_ARG_WITH([libxml], AC_HELP_STRING([--with-libxml=@<:@PFX@:>@], [libxml2 location]))
if test "x$with_libxml" = "xno" ; then
    AC_MSG_CHECKING(for libxml2 libraries >= $LIBXML_REQUIRED)
    AC_MSG_ERROR([libxml2 >= $LIBXML_REQUIRED is required for libstoragemgmt])
elif test "x$with_libxml" = "x" && test "x$PKG_CONFIG" != "x" ; then
    PKG_CHECK_MODULES(LIBXML, libxml-2.0 >= $LIBXML_REQUIRED, [LIBXML_FOUND=yes], [LIBXML_FOUND=no])
fi
if test "$LIBXML_FOUND" = "no" ; then
    if test "x$with_libxml" != "x" ; then
	LIBXML_CONFIG=$with_libxml/bin/$LIBXML_CONFIG
    fi
    AC_MSG_CHECKING(libxml2 $LIBXML_CONFIG >= $LIBXML_REQUIRED )
    if ! $LIBXML_CONFIG --version > /dev/null 2>&1 ; then
	AC_MSG_ERROR([Could not find libxml2 anywhere (see config.log for details).])
    fi
    vers=`$LIBXML_CONFIG --version | awk -F. '{ printf "%d", ($1 * 1000 + $2) * 1000 + $3;}'`
    minvers=`echo $LIBXML_REQUIRED | awk -F. '{ printf "%d", ($1 * 1000 + $2) * 1000 + $3;}'`
    if test "$vers" -ge "$minvers" ; then
        LIBXML_LIBS="`$LIBXML_CONFIG --libs`"
        LIBXML_CFLAGS="`$LIBXML_CONFIG --cflags`"
	LIBXML_FOUND="yes"
        AC_MSG_RESULT(yes)
    else
        AC_MSG_ERROR(
          [You need at least libxml2 $LIBXML_REQUIRED for libstoragemgmt])
    fi
fi

AC_SUBST([LIBXML_CFLAGS])
AC_SUBST([LIBXML_LIBS])

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

AC_SUBST(PEGASUS_CFLAGS)
AC_SUBST(PEGASUS_LIBS)

#Check for python as it is needed for the daemon
AM_PATH_PYTHON([2.6], [], AC_MSG_ERROR([Python interpreter 2.6 or later required]) )

AC_OUTPUT(Makefile src/Makefile plugin/Makefile doc/Makefile \
          tools/Makefile tools/lsmcli/Makefile tools/smiscli/Makefile \
          include/Makefile include/libstoragemgmt/Makefile lsm/Makefile)
