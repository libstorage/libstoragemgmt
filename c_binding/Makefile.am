SUBDIRS = include

AM_CPPFLAGS = 	-I$(top_srcdir)/c_binding/include \
				-I$(top_builddir)/c_binding/include \
				$(LIBGLIB_CFLAGS) \
				$(LIBUDEV_CFLAGS) \
				$(LIBLED_CFLAGS)

lib_LTLIBRARIES = libstoragemgmt.la

libstoragemgmt_la_LIBADD=$(LIBXML_LIBS) $(LIBGLIB_LIBS) $(LIBUDEV_LIBS) $(LIBLED_LIBS)

libstoragemgmt_la_LDFLAGS= -version-info $(LIBSM_LIBTOOL_VERSION)
libstoragemgmt_la_SOURCES= \
	lsm_mgmt.cpp lsm_datatypes.hpp lsm_datatypes.cpp lsm_convert.hpp \
	lsm_convert.cpp lsm_ipc.hpp lsm_ipc.cpp lsm_plugin_ipc.hpp \
	lsm_plugin_ipc.cpp uri_parser.hpp \
	utils.c utils.h libsg.c libsg.h lsm_local_disk.cpp \
	libata.c libata.h libsas.c libsas.h libfc.c libfc.h \
	libiscsi.c libiscsi.h libnvme.c libnvme.h

EXTRA_DIST = jsmn.h lsm_value_jsmn.hpp
