INCLUDES = -I$(top_srcdir)/include -I@srcdir@/include

EXTRA_DIST=cmdtest.py runtests.sh

TESTS = runtests.sh

check_PROGRAMS = tester
tester_CFLAGS = $(LIBCHECK_CFLAGS)
tester_LDADD = ../src/libstoragemgmt.la $(LIBCHECK_LIBS)
tester_SOURCES = tester.c
