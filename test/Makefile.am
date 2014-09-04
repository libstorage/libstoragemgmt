AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include \
	$(LIBXML_CFLAGS)

EXTRA_DIST=cmdtest.py runtests.sh plugin_test.py

TESTS = runtests.sh

check_PROGRAMS = tester
tester_CFLAGS = $(LIBCHECK_CFLAGS)
tester_LDADD = ../c_binding/libstoragemgmt.la $(LIBCHECK_LIBS)
tester_SOURCES = tester.c
