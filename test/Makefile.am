AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include

EXTRA_DIST=cmdtest.py plugin_test.py test_include.sh runtests.sh.in integration.sh \
	test_transport.py test_pluginrunner.py test_megaraid.py lsm_stub.py

if WITH_TEST
all: tester test_ipc test_conn_limit

check_PROGRAMS = tester test_ipc test_conn_limit
tester_CFLAGS = $(LIBCHECK_CFLAGS)
tester_LDADD = ../c_binding/libstoragemgmt.la $(LIBCHECK_LIBS)
tester_SOURCES = tester.c

# Built directly against lsm_ipc.cpp (rather than libstoragemgmt.la) since
# Transport/Ipc have hidden visibility and are not exported from the shared
# library.
test_ipc_CXXFLAGS = $(LIBCHECK_CFLAGS) -pthread
test_ipc_LDFLAGS = -pthread
test_ipc_LDADD = $(LIBCHECK_LIBS)
test_ipc_SOURCES = test.cpp ../c_binding/lsm_ipc.cpp ../c_binding/lsm_ipc.hpp

# The per-uid connection limit lives in its own module precisely so it can be
# tested without a running daemon; lsmd itself is a program, not a library.
test_conn_limit_CFLAGS = $(LIBCHECK_CFLAGS) -I$(top_srcdir)/daemon
test_conn_limit_LDADD = $(LIBCHECK_LIBS)
test_conn_limit_SOURCES = test_conn_limit.c ../daemon/conn_limit.c \
	../daemon/conn_limit.h
endif
