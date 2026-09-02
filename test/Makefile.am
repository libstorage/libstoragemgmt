AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include

EXTRA_DIST=cmdtest.py plugin_test.py test_include.sh runtests.sh.in integration.sh \
	test_transport.py

if WITH_TEST
all: tester test_ipc

check_PROGRAMS = tester test_ipc
tester_CFLAGS = $(LIBCHECK_CFLAGS)
tester_LDADD = ../c_binding/libstoragemgmt.la $(LIBCHECK_LIBS)
tester_SOURCES = tester.c

# Built directly against lsm_ipc.cpp (rather than libstoragemgmt.la) since
# Transport/Ipc have hidden visibility and are not exported from the shared
# library.
test_ipc_CXXFLAGS = $(LIBCHECK_CFLAGS)
test_ipc_LDADD = $(LIBCHECK_LIBS)
test_ipc_SOURCES = test.cpp ../c_binding/lsm_ipc.cpp ../c_binding/lsm_ipc.hpp
endif
