INCLUDES = -I$(top_srcdir)/include -I$(top_builddir)/include -I@srcdir@/include $(LIBXML_CFLAGS)

lib_LTLIBRARIES = libstoragemgmt.la

libstoragemgmt_la_LIBADD=$(LIBXML_LIBS) $(YAJL_LIBS)
libstoragemgmt_la_LDFLAGS=
libstoragemgmt_la_SOURCES=lsm_mgmt.cpp lsm_datatypes.hpp lsm_datatypes.cpp \
                          lsm_convert.hpp lsm_convert.cpp lsm_ipc.hpp lsm_ipc.cpp \
                          lsm_plugin_ipc.hpp lsm_plugin_ipc.cpp

DEPS = $(top_builddir)/src/libstoragemgmt.la

