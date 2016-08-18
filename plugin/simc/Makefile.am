AM_CPPFLAGS = \
	-I$(top_srcdir)/c_binding/include \
	-I@srcdir@/c_binding/include \
	$(DEFS) $(SQLITE3_CFLAGS)

bin_PROGRAMS = simc_lsmplugin

simc_lsmplugin_LDADD = \
	../../c_binding/libstoragemgmt.la \
	$(SQLITE3_LIBS) $(SSL_LIBS)

simc_lsmplugin_SOURCES = \
	utils.c utils.h \
	db.h db.c db_table_init.h \
	mgm_ops.h mgm_ops.c \
	san_ops.h san_ops.c \
	fs_ops.h fs_ops.c \
	nfs_ops.h nfs_ops.c \
	ops_v1_2.h ops_v1_2.c \
	ops_v1_3.h ops_v1_3.c \
	vector.h vector.c \
	simc_lsmplugin.c
