AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include

EXTRA_DIST=cmdtest.py plugin_test.py test_include.sh runtests.sh.in integration.sh \
	test_transport.py test_pluginrunner.py test_megaraid.py \
	test_daemon_conn_limit.py lsm_stub.py conftest.py devel_env.sh docker_ci_test.sh

if WITH_TEST
all: tester test_ipc test_conn_limit test_libsg

check_PROGRAMS = tester test_ipc test_conn_limit test_libsg
tester_CFLAGS = $(LIBCHECK_CFLAGS)
tester_LDADD = ../c_binding/libstoragemgmt.la $(LIBCHECK_LIBS)
tester_SOURCES = tester.c

# Built directly against lsm_ipc.cpp (rather than libstoragemgmt.la) since
# Transport/Ipc have hidden visibility and are not exported from the shared
# library.
test_ipc_CXXFLAGS = $(LIBCHECK_CFLAGS) -pthread
test_ipc_LDFLAGS = -pthread -Wl,--wrap=clock_gettime
test_ipc_LDADD = $(LIBCHECK_LIBS)
test_ipc_SOURCES = test.cpp ../c_binding/lsm_ipc.cpp ../c_binding/lsm_ipc.hpp

# The per-uid connection limit lives in its own module precisely so it can be
# tested without a running daemon; lsmd itself is a program, not a library.
test_conn_limit_CFLAGS = $(LIBCHECK_CFLAGS) -I$(top_srcdir)/daemon
test_conn_limit_LDADD = $(LIBCHECK_LIBS)
test_conn_limit_SOURCES = test_conn_limit.c ../daemon/conn_limit.c \
	../daemon/conn_limit.h

# libsg.c's sense data helpers and libsas.c are LSM_DLL_LOCAL, so they are
# compiled in directly (with the utils.c and libata.c they call) rather than
# reached through libstoragemgmt.la.
test_libsg_CFLAGS = $(LIBCHECK_CFLAGS) -I$(top_srcdir)/c_binding
test_libsg_LDADD = $(LIBCHECK_LIBS)
test_libsg_SOURCES = test_libsg.c ../c_binding/libsg.c ../c_binding/libsg.h \
	../c_binding/utils.c ../c_binding/utils.h ../c_binding/libata.c \
	../c_binding/libata.h ../c_binding/libsas.c ../c_binding/libsas.h
endif
