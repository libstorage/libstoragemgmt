AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include \
	$(DEFS) \
	$(LIBUDEV_CFLAGS)

bin_PROGRAMS = ses_lsmplugin

ses_lsmplugin_LDADD = \
	../../c_binding/libstoragemgmt.la \
	-lsgutils2 $(LIBUDEV_LIBS)

ses_lsmplugin_SOURCES = \
	ptr_list.h ptr_list.c \
	my_mem.h my_mem.c \
	libses.h libses.c \
	libudev_storage.h libudev_storage.c \
	ses_lsmplugin.c
