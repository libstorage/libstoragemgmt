AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include

EXTRA_DIST=cmdtest.py plugin_test.py test_include.sh runtests.sh.in integration.sh \
	test_transport.py test_rpc_dispatch.py lsm_stub.py test_nfs_clib.py

if WITH_TEST
check_PROGRAMS = tester test_ipc test_libsg test_plugin_ipc
tester_CFLAGS = $(LIBCHECK_CFLAGS)
tester_LDADD = ../c_binding/libstoragemgmt.la $(LIBCHECK_LIBS)
tester_SOURCES = tester.c

# Built directly against lsm_ipc.cpp (rather than libstoragemgmt.la) since
# Transport/Ipc have hidden visibility and are not exported from the shared
# library.
test_ipc_CXXFLAGS = $(LIBCHECK_CFLAGS)
test_ipc_LDADD = $(LIBCHECK_LIBS)
test_ipc_SOURCES = test.cpp ../c_binding/lsm_ipc.cpp ../c_binding/lsm_ipc.hpp

# libsg.c's sense data helpers and libsas.c are LSM_DLL_LOCAL, so they are
# compiled in directly (with the utils.c and libata.c they call) rather than
# reached through libstoragemgmt.la.
# ioctl is wrapped so the SG_IO paths (_sg_io_vpd, _sg_io_mode_sense,
# _sg_request_sense, ...) can be driven with canned device responses instead
# of real hardware.
test_libsg_CFLAGS = $(LIBCHECK_CFLAGS) -I$(top_srcdir)/c_binding
test_libsg_LDFLAGS = -Wl,--wrap=ioctl
test_libsg_LDADD = $(LIBCHECK_LIBS)
test_libsg_SOURCES = test_libsg.c ../c_binding/libsg.c ../c_binding/libsg.h \
	../c_binding/utils.c ../c_binding/utils.h ../c_binding/libata.c \
	../c_binding/libata.h ../c_binding/libsas.c ../c_binding/libsas.h

# Built directly against the plug-in-side IPC sources (rather than
# libstoragemgmt.la) for two reasons: __wrap_strdup (below) only intercepts
# calls made from object files linked into this binary, not calls already
# resolved inside the shared library; and AddressSanitizer's leak detector
# needs every allocation on the path it is checking instrumented the same
# way. -Wl,--wrap=strdup lets the NULL-conversion tests force
# lsm_volume_record_alloc()/lsm_system_record_alloc() to fail without
# failing allocations globally; -fsanitize=address,leak catches the
# fs_snapshots leak this covers (fae61750).
test_plugin_ipc_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/c_binding \
	$(LIBGLIB_CFLAGS) $(LIBUDEV_CFLAGS)
test_plugin_ipc_CXXFLAGS = $(LIBCHECK_CFLAGS) -pthread -fsanitize=address
test_plugin_ipc_LDFLAGS = -pthread -fsanitize=address -Wl,--wrap=strdup
test_plugin_ipc_LDADD = $(LIBCHECK_LIBS) $(LIBGLIB_LIBS) $(LIBUDEV_LIBS) \
	../c_binding/libstoragemgmt.la
test_plugin_ipc_SOURCES = test_plugin_ipc.cpp \
	../c_binding/lsm_plugin_ipc.cpp ../c_binding/lsm_plugin_ipc.hpp \
	../c_binding/lsm_convert.cpp ../c_binding/lsm_convert.hpp \
	../c_binding/lsm_datatypes.cpp ../c_binding/lsm_datatypes.hpp \
	../c_binding/lsm_ipc.cpp ../c_binding/lsm_ipc.hpp

if WITH_NFS
check_PROGRAMS += test_nfs_clib_py_in_list

# py_in_list() is 'static' in nfs_clib.c, so this #includes that file
# directly (see test_nfs_clib_py_in_list.c) rather than linking against
# nfs_plugin's nfs_clib.la; it embeds the interpreter (Py_Initialize())
# instead of being loaded by it, hence PYTHON_EMBED_* rather than the
# PYTHON_* flags used to build nfs_clib.la itself.
test_nfs_clib_py_in_list_CFLAGS = $(PYTHON_EMBED_CFLAGS) \
	-I$(top_srcdir)/c_binding/include
test_nfs_clib_py_in_list_LDADD = $(PYTHON_EMBED_LIBS)
test_nfs_clib_py_in_list_SOURCES = test_nfs_clib_py_in_list.c
endif

all: $(check_PROGRAMS)
# Built directly against the plug-in-side IPC sources (rather than
# libstoragemgmt.la) for two reasons: __wrap_strdup (below) only intercepts
# calls made from object files linked into this binary, not calls already
# resolved inside the shared library; and AddressSanitizer's leak detector
# needs every allocation on the path it is checking instrumented the same
# way. -Wl,--wrap=strdup lets the NULL-conversion tests force
# lsm_volume_record_alloc()/lsm_system_record_alloc() to fail without
# failing allocations globally; -fsanitize=address,leak catches the
# fs_snapshots leak this covers (fae61750).
test_plugin_ipc_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/c_binding \
	$(LIBGLIB_CFLAGS) $(LIBUDEV_CFLAGS)
test_plugin_ipc_CXXFLAGS = $(LIBCHECK_CFLAGS) -pthread -fsanitize=address
test_plugin_ipc_LDFLAGS = -pthread -fsanitize=address -Wl,--wrap=strdup
test_plugin_ipc_LDADD = $(LIBCHECK_LIBS) $(LIBGLIB_LIBS) $(LIBUDEV_LIBS) \
	../c_binding/libstoragemgmt.la
test_plugin_ipc_SOURCES = test_plugin_ipc.cpp \
	../c_binding/lsm_plugin_ipc.cpp ../c_binding/lsm_plugin_ipc.hpp \
	../c_binding/lsm_convert.cpp ../c_binding/lsm_convert.hpp \
	../c_binding/lsm_datatypes.cpp ../c_binding/lsm_datatypes.hpp \
	../c_binding/lsm_ipc.cpp ../c_binding/lsm_ipc.hpp

endif
