AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I$(top_builddir)/c_binding/include \
	-I@srcdir@/c_binding/include \
	$(LIBXML_CFLAGS) $(LIBGLIB_CFLAGS)

bin_PROGRAMS = lsmd

DEPS = $(top_builddir)/c_binding/libstoragemgmt.la

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
