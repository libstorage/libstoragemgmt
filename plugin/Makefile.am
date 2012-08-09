INCLUDES = -I$(top_srcdir)/include -I@srcdir@/include \
            $(LIBXML_CFLAGS) $(DEFS) $(LIBGLIB_CFLAGS)

bin_PROGRAMS = simc_lsmplugin

simc_lsmplugin_LDADD= ../src/libstoragemgmt.la -lcrypt $(LIBGLIB_LIBS)
simc_lsmplugin_SOURCES=                \
     simc_lsmplugin.c
