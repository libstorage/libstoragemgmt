AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include \
	$(DEFS) $(LIBGLIB_CFLAGS)

bin_PROGRAMS = simc_lsmplugin

simc_lsmplugin_LDADD = \
	../../c_binding/libstoragemgmt.la \
	$(LIBGLIB_LIBS) $(SSL_LIBS)
simc_lsmplugin_SOURCES = simc_lsmplugin.c
