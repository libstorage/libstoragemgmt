INCLUDES = -I$(top_srcdir)/include -I@srcdir@/include \
            $(LIBXML_CFLAGS) $(PEGASUS_CFLAGS) $(DEFS) $(LIBGLIB_CFLAGS)

bin_PROGRAMS = simc_lsmplugin

simc_lsmplugin_LDADD= ../src/libstoragemgmt.la -lcrypt $(LIBGLIB_LIBS)
simc_lsmplugin_SOURCES=                \
     simc_lsmplugin.c

if BUILD_SMIS_CPP
bin_PROGRAMS += smis_lsmplugin

smis_lsmplugin_LDADD= ../src/libstoragemgmt.la $(PEGASUS_LIBS)
smis_lsmplugin_SOURCES=                 \
     lsm_plugin_smis.cpp                \
     smis.cpp smis.h                    \
     util/qparams.c util/qparams.h      \
     util/misc.cpp util/misc.h
endif
