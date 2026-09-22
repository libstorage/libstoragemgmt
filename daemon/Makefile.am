bin_PROGRAMS = lsmd

EXTRA_DIST=

lsmd_LDFLAGS=-Wl,-z,relro,-z,now -pie $(LIBCONFIG_LIBS)
lsmd_CFLAGS=-fPIE -DPIE -I$(top_srcdir)/c_binding $(LIBCONFIG_CFLAGS)

lsmd_SOURCES = lsm_daemon.c
