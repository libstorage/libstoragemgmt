SUBDIRS = include

AM_CPPFLAGS = 	-I$(top_srcdir)/c_binding/include \
				-I$(top_builddir)/c_binding/include \
				-I@srcdir@/c_binding/include \
				$(LIBXML_CFLAGS) $(LIBGLIB_CFLAGS)

lib_LTLIBRARIES = libstoragemgmt.la

libstoragemgmt_la_LIBADD=$(LIBXML_LIBS) $(YAJL_LIBS) $(LIBGLIB_LIBS)
libstoragemgmt_la_LDFLAGS= -version-info $(LIBSM_LIBTOOL_VERSION)
libstoragemgmt_la_SOURCES= \
	lsm_mgmt.cpp lsm_datatypes.hpp lsm_datatypes.cpp lsm_convert.hpp \
	lsm_convert.cpp lsm_ipc.hpp lsm_ipc.cpp lsm_plugin_ipc.hpp \
	lsm_plugin_ipc.cpp util/qparams.c util/qparams.h
