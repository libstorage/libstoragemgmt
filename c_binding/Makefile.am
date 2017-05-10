SUBDIRS = include

AM_CPPFLAGS = 	-I$(top_srcdir)/c_binding/include \
				-I$(top_builddir)/c_binding/include \
				-I@srcdir@/c_binding/include \
				$(LIBXML_CFLAGS) \
				$(LIBUDEV_CFLAGS)

lib_LTLIBRARIES = libstoragemgmt.la

libstoragemgmt_la_LIBADD=$(LIBXML_LIBS) $(YAJL_LIBS) $(LIBUDEV_LIBS)
libstoragemgmt_la_LDFLAGS= -version-info $(LIBSM_LIBTOOL_VERSION)
libstoragemgmt_la_SOURCES= \
	lsm_mgmt.cpp lsm_datatypes.hpp lsm_datatypes.cpp lsm_convert.hpp \
	lsm_convert.cpp lsm_ipc.hpp lsm_ipc.cpp lsm_plugin_ipc.hpp \
	lsm_plugin_ipc.cpp util/qparams.c util/qparams.h \
	utils.c utils.h libsg.c libsg.h lsm_local_disk.c libses.c libses.h \
	libata.c libata.h libsas.c libsas.h libfc.c libfc.h \
	libiscsi.c libiscsi.h ptr_array.h hash_table.h

if USE_GLIB
libstoragemgmt_la_SOURCES += ptr_array_glib.c hash_table_glib.c
libstoragemgmt_la_LIBADD += $(LIBGLIB_LIBS)
AM_CPPFLAGS += $(LIBGLIB_CFLAGS)
else
libstoragemgmt_la_SOURCES += ptr_array_utlist.c utlist.h \
			     hash_table_uthash.c uthash.h
endif

EXTRA_DIST = ptr_array_utlist.c utlist.h ptr_array_glib.c \
	     hash_table_uthash.c uthash.h hash_table_glib.c
