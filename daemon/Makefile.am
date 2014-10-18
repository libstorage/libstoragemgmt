bin_PROGRAMS = lsmd

EXTRA_DIST=lsm_rest.c lsm_rest.h

lsmd_LDFLAGS=-Wl,-z,relro,-z,now -pie
lsmd_CFLAGS=-fPIE -DPIE

lsmd_SOURCES = lsm_daemon.c

if WITH_REST_API
bin_PROGRAMS += lsm_restd
lsm_restd_LDFLAGS=$(LIBMICROHTTPD_LIBS) $(JSON_LIBS) $(LIBXML_LIBS)
lsm_restd_CFLAGS=-fPIE -DPIE  $(LIBMICROHTTPD_CFLAGS) $(JSON_CFLAGS) $(LIBXML_CFLAGS)
lsm_restd_SOURCES= lsm_rest.c
endif
