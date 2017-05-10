AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include \
	$(LIBXML_CFLAGS)

EXTRA_DIST=cmdtest.py plugin_test.py test_include.sh runtests.sh.in

if WITH_TEST
all: tester string_list_hash_test

check_PROGRAMS = tester string_list_hash_test
tester_CFLAGS = $(LIBCHECK_CFLAGS)
tester_LDADD = ../c_binding/libstoragemgmt.la $(LIBCHECK_LIBS)
tester_SOURCES = tester.c

string_list_hash_test_LDADD = ../c_binding/libstoragemgmt.la
string_list_hash_test_SOURCES = string_list_hash_test.c string_list_hash_test.h

endif
