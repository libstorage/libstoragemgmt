AM_CPPFLAGS = \
	-std=gnu99 \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include \
	$(DEFS)

bin_PROGRAMS = nfs_lsmplugin

nfs_lsmplugin_LDADD = \
	../../c_binding/libstoragemgmt.la \
	$(SSL_LIBS)

nfs_lsmplugin_SOURCES = \
	nfs_lsmplugin.c
