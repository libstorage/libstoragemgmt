dnl Process this file with autoconf to produce a configure script.
dnl Copyright (C) 2011-2016 Red Hat, Inc.
dnl See COPYING.LIB for the License of this software

AC_INIT(
    [libstoragemgmt], [1.5.0], [libstoragemgmt-devel@lists.fedorahosted.org],
    [], [https://github.com/libstorage/libstoragemgmt/])
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
#Check for sqlite development libs for simc_lsmplugin
PKG_CHECK_MODULES([SQLITE3], [sqlite3])

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

AC_ARG_WITH([test],
    [AC_HELP_STRING([--without-test],
                    [disable all test case])],
    [], [with_test=yes])

AM_CONDITIONAL([WITH_TEST], [test "x$with_test" = "xyes"])

if test "x${with_test}" = "xyes"; then
    PKG_CHECK_MODULES([LIBCHECK], [check >= 0.9.8 ])

    dnl =====================================================================
    dnl Check for perl, used for code constants checking
    dnl =====================================================================
    AC_PATH_PROGS(PERL, perl)
    if test -z $PERL ;then
        AC_MSG_ERROR([Need 'perl' to run test cases])
    fi

    dnl =====================================================================
    dnl Check for chrpath, valgrind, wc, used for make check
    dnl =====================================================================
    AC_PATH_PROG([CHRPATH], chrpath)
    if test -z $CHRPATH ;then
        AC_MSG_ERROR([Need 'chrpath' to run test cases])
    fi

    AC_ARG_WITH([mem-leak-test],
        [AS_HELP_STRING([--without-mem-leak-test],
            [Do not run memory leak test])],
        [],
        [with_mem_leak_test=yes])

    if test "x$with_mem_leak_test" == "xyes"; then
        AC_PATH_PROG([VALGRIND], valgrind)
        AC_SUBST(WITH_MEM_LEAK_TEST, yes)
        if test -z $VALGRIND ;then
            AC_MSG_ERROR([Need 'valgrind' to run memory leak test])
        fi
    else
        AC_SUBST(WITH_MEM_LEAK_TEST, no)
    fi

    AC_PATH_PROG([WC], wc)
    if test -z $WC ;then
        AC_MSG_ERROR([Need 'wc' to run test cases])
    fi
    AC_PATH_PROG([PS], ps)
    if test -z $PS ;then
        AC_MSG_ERROR([Need 'ps' to run test cases])
    fi
fi

PKG_CHECK_MODULES([LIBUDEV], [libudev])

dnl ==========================================================================
dnl See if the user wants to use python3
dnl ==========================================================================

AC_ARG_WITH([python3],
    [AS_HELP_STRING([--with-python3],
        [Use Python3 only])],
    [with_python3=yes],
    [])

if test "x$with_python3" = "xyes"; then
    AC_PATH_PROGS(PYTHON, python3, [Python3 is required])
    AM_PATH_PYTHON([3], [], AC_MSG_ERROR([Python interpreter 3 required]))
    AC_PYTHON_MODULE([lmiwbem], [], [python3])
    AC_PYTHON_MODULE([pywbem], [], [python3])
    if test "x$HAVE_PYMOD_LMIWBEM" == "xno" && \
       test "x$HAVE_PYMOD_PYWBEM" == "xno" ;then
        AC_MSG_ERROR([Neither python3 module 'lmiwbem' nor 'pywbem' found, \
please install any of them.])
    fi

    AC_PYTHON_MODULE([six], [Required], [python3])
    AC_PYTHON_MODULE([pyudev], [Required], [python3])
    PKG_CHECK_MODULES([PYTHON], [python3], [], [not_found_py_pkg=yes])

    if test "x$not_found_py_pkg" == "xyes"; then
        AC_CHECK_PROG([PY_CONFIG_CHECK], [python3-config], [yes])
        if test "x${PY_CONFIG_CHECK}" != "xyes"; then
            AC_MSG_ERROR([Python3 development libraries required])
        fi
        PYTHON_CFLAGS=`python3-config --cflags`
        PYTHON_LIBS=`python3-config --libs`
        AC_SUBST(PYTHON_CFLAGS)
        AC_SUBST(PYTHON_LIBS)
    fi

    if test -e "/etc/debian_version"; then
        new_py_dir=`echo $pythondir| sed -e 's/site-packages$/dist-packages/'`
        AC_SUBST([pythondir], [$new_py_dir])
        AC_MSG_NOTICE(["Applying workaround for debian bug 839064: $pythondir"])
    fi
    AC_SUBST(PY_VERSION, 3)
else
    AC_PATH_PROGS(PYTHON, python2.7 python2.6 python, [Python is required])
    AC_MSG_CHECKING([Check for Python major version])
    PYTHON_MAJOR_VERSION=`$PYTHON -c "import sys; print(sys.version_info[[0]])"`
    case "$PYTHON_MAJOR_VERSION" in
        2)
            ;;
        *)
            AC_MSG_ERROR([we need Python version 2.x \
but found $PYTHON_MAJOR_VERSION.x]) ;;
    esac
    AC_MSG_RESULT([$PYTHON_MAJOR_VERSION])
    AM_PATH_PYTHON([2.6], [],
                   AC_MSG_ERROR([Python interpreter 2.6 or 2.7 required]) )

    AC_PYTHON_MODULE([lmiwbem])
    AC_PYTHON_MODULE([pywbem])
    if test "x$HAVE_PYMOD_LMIWBEM" == "xno" && \
       test "x$HAVE_PYMOD_PYWBEM" == "xno" ;then
        AC_MSG_ERROR([Neither python2 module 'lmiwbem' nor 'pywbem' found, \
please install any of them.])
    fi
    AC_PYTHON_MODULE([argparse], [Required])
    AC_PYTHON_MODULE([pyudev], [Required])
    AC_PYTHON_MODULE([six], [Required])
    PKG_CHECK_MODULES([PYTHON], [python2], [], [not_found_py_pkg=yes])

    if test "x$not_found_py_pkg" == "xyes"; then
        AC_CHECK_PROG([PY_CONFIG_CHECK], [python-config], [yes])
        if test "x${PY_CONFIG_CHECK}" != "xyes"; then
            AC_MSG_ERROR([Python2 development libraries required])
        fi
        PYTHON_CFLAGS=`python-config --cflags`
        PYTHON_LIBS=`python-config --libs`
        AC_SUBST(PYTHON_CFLAGS)
        AC_SUBST(PYTHON_LIBS)
    fi
    AC_SUBST(PY_VERSION, 2)
fi
AM_CONDITIONAL([WITH_PYTHON3], [test "x$with_python3" = "xyes"])

dnl ==========================================================================
dnl Add option '--without-megaraid' to exclude megaraid plugin.
dnl ==========================================================================

AC_ARG_WITH([megaraid],
    [AS_HELP_STRING([--without-megaraid],
        [Do not build the MegaRAID plugin])],
    [],
    [with_megaraid=yes])

AM_CONDITIONAL([WITH_MEGARAID], [test "x$with_megaraid" = "xyes"])

dnl ==========================================================================
dnl Add option '--without-hpsa' to exclude hpsa plugin.
dnl ==========================================================================

AC_ARG_WITH([hpsa],
    [AS_HELP_STRING([--without-hpsa],
        [Do not build the HP SmartArray plugin])],
    [],
    [with_hpsa=yes])

AM_CONDITIONAL([WITH_HPSA], [test "x$with_hpsa" = "xyes"])

dnl ==========================================================================
dnl Add option '--without-hpsa' to exclude hpsa plugin.
dnl ==========================================================================

AC_ARG_WITH([arcconf],
    [AS_HELP_STRING([--without-arcconf],
        [Do not build the Microsemi Arcconf plugin])],
    [],
    [with_arcconf=yes])

AM_CONDITIONAL([WITH_ARCCONF], [test "x$with_arcconf" = "xyes"])

dnl ==========================================================================
dnl Check for libconfig as it is needed for lsmd daemon
dnl ==========================================================================
PKG_CHECK_MODULES(
    [LIBCONFIG], [libconfig >= 1.3.2],,
    AC_MSG_ERROR([libconfig 1.3.2 or newer not found.])
)

dnl ==========================================================================
dnl Add option '--without-bash-completion' to exclude bash completion script.
dnl ==========================================================================

AC_ARG_WITH([bash-completion],
    [AS_HELP_STRING([--without-bash-completion],
        [Do not install the bash auto-completion script])],
    [],
    [with_bash_completion=yes])

AM_CONDITIONAL(
    [WITH_BASH_COMPLETION], [test "x$with_bash_completion" = "xyes"])

dnl ==========================================================================
dnl Add option '--with-bash-completion-dir' to specific bash completion dir,
dnl if not defined, if pkg-config file for bash-completion found, use its
dnl 'completionsdir', else use /etc/bash_completion.d
dnl ==========================================================================
AC_ARG_WITH([bash-completion-dir],
    AS_HELP_STRING(
        [--with-bash-completion-dir=DIR],
        [Bash completions directory]),
        [],
        [AS_IF(
            [$($PKG_CONFIG --exists bash-completion)],
            [with_bash_completion_dir=$(
                $PKG_CONFIG --variable=completionsdir bash-completion)],
            # EPEL 6 is still shipping bash-completion version 1.x
            # which does not provide pkg-config support.
            # So, for EPEL 6 or anyone not installed bash-compeltion, we
            # use fallback folder '/etc/bash_completion.d'
            [with_bash_completion_dir=$sysconfdir/bash_completion.d])])

AC_SUBST([bashcompletiondir], [$with_bash_completion_dir])

#Setup the unit directory for systemd stuff
PKG_PROG_PKG_CONFIG
AC_ARG_WITH([systemdsystemunitdir],
        AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
        [], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != xno; then
        AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != xno ])

AX_CHECK_COMPILE_FLAG([-Wall], [CFLAGS="$CFLAGS -Wall"], [], [])
AX_CHECK_COMPILE_FLAG([-Werror], [CFLAGS="$CFLAGS -Werror"], [], [])
AX_CHECK_COMPILE_FLAG([-Wextra], [CFLAGS="$CFLAGS -Wextra"], [], [])

AC_CONFIG_FILES([
    libstoragemgmt.pc
    Makefile
    c_binding/Makefile
    c_binding/include/Makefile
    c_binding/include/libstoragemgmt/Makefile
    c_binding/include/libstoragemgmt/libstoragemgmt_version.h
    python_binding/Makefile
    python_binding/lsm/version.py
    plugin/Makefile
    plugin/simc/Makefile
    plugin/megaraid/Makefile
    plugin/hpsa/Makefile
    plugin/arcconf/Makefile
    daemon/Makefile
    config/Makefile
    doc/Makefile
    doc/man/lsmcli.1
    doc/man/lsmd.1
    doc/man/sim_lsmplugin.1
    doc/man/simc_lsmplugin.1
    doc/man/smispy_lsmplugin.1
    doc/man/ontap_lsmplugin.1
    doc/man/targetd_lsmplugin.1
    doc/man/nstor_lsmplugin.1
    doc/doxygen.conf
    doc/man/lsmd.conf.5
    doc/man/arcconf_lsmplugin.1
    doc/man/megaraid_lsmplugin.1
    doc/man/hpsa_lsmplugin.1
    tools/Makefile
    tools/udev/Makefile
    tools/lsmcli/Makefile
    tools/utility/Makefile
    tools/bash_completion/Makefile
    packaging/Makefile
    packaging/daemon/Makefile
    packaging/libstoragemgmt.spec
    doc/man/Makefile
    test/Makefile])

if test "x${with_test}" = "xyes"; then
    AC_CONFIG_FILES([test/runtests.sh], [chmod +x test/runtests.sh])
fi

AC_CONFIG_FILES([plugin/sim/sim_lsmplugin],
    [chmod +x plugin/sim/sim_lsmplugin])
AC_CONFIG_FILES([plugin/megaraid/megaraid_lsmplugin],
    [chmod +x plugin/megaraid/megaraid_lsmplugin])
AC_CONFIG_FILES([plugin/hpsa/hpsa_lsmplugin],
    [chmod +x plugin/hpsa/hpsa_lsmplugin])
AC_CONFIG_FILES([plugin/arcconf/arcconf_lsmplugin],
    [chmod +x plugin/arcconf/arcconf_lsmplugin])
AC_CONFIG_FILES([plugin/ontap/ontap_lsmplugin],
    [chmod +x plugin/ontap/ontap_lsmplugin])
AC_CONFIG_FILES([plugin/nstor/nstor_lsmplugin],
    [chmod +x plugin/nstor/nstor_lsmplugin])
AC_CONFIG_FILES([plugin/targetd/targetd_lsmplugin],
    [chmod +x plugin/targetd/targetd_lsmplugin])
AC_CONFIG_FILES([plugin/smispy/smispy_lsmplugin],
    [chmod +x plugin/smispy/smispy_lsmplugin])
AC_CONFIG_FILES([tools/lsmcli/lsmcli],
    [chmod +x tools/lsmcli/lsmcli])
AC_CONFIG_FILES([test/plugin_test.py],
    [chmod +x test/plugin_test.py])
AC_CONFIG_FILES([test/cmdtest.py],
    [chmod +x test/cmdtest.py])

AC_OUTPUT
