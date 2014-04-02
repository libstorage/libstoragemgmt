AM_CPPFLAGS= -I$(top_srcdir)/include -I@srcdir@/include \
                $(DEFS) $(LIBGLIB_CFLAGS)

bin_PROGRAMS = simc_lsmplugin

simc_lsmplugin_LDADD= ../src/libstoragemgmt.la $(LIBGLIB_LIBS) $(LIBSSL_LIBS)
simc_lsmplugin_SOURCES=                \
     simc_lsmplugin.c
