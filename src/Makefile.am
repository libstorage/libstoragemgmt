AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include -I@srcdir@/include $(LIBXML_CFLAGS) $(LIBGLIB_CFLAGS)

lib_LTLIBRARIES = libstoragemgmt.la

bin_PROGRAMS = lsmd

libstoragemgmt_la_LIBADD=$(LIBXML_LIBS) $(YAJL_LIBS) $(LIBGLIB_LIBS)
libstoragemgmt_la_LDFLAGS=
libstoragemgmt_la_SOURCES=lsm_mgmt.cpp lsm_datatypes.hpp lsm_datatypes.cpp \
                          lsm_convert.hpp lsm_convert.cpp lsm_ipc.hpp lsm_ipc.cpp \
                          lsm_plugin_ipc.hpp lsm_plugin_ipc.cpp util/qparams.c \
			  util/qparams.h

DEPS = $(top_builddir)/src/libstoragemgmt.la

EXTRA_DIST=lsm_rest.c lsm_rest.h

lsmd_LDFLAGS=-Wl,-z,relro,-z,now -pie
lsmd_CFLAGS=-fPIE -DPIE

lsmd_SOURCES = lsm_daemon.c

if WITH_REST_API
bin_PROGRAMS += lsm_restd
lsm_restd_LDFLAGS=$(LIBMICROHTTPD_LIBS) $(JSON_LIBS) $(LIBXML_LIBS)
lsm_restd_CFLAGS=-fPIE -DPIE
lsm_restd_SOURCES= lsm_rest.c
endif
