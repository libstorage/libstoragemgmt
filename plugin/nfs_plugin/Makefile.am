if WITH_NFS

nfsdir = $(LSM_PYSO_DIR)/nfs_plugin
pysodir = $(LSM_PYSO_DIR)/nfs_plugin

nfs_PYTHON = __init__.py nfs.py

if WITH_PYTHON3
_PY_CLIB_INIT_NAME = "PyInit_nfs_clib"
else
_PY_CLIB_INIT_NAME = "initnfs_clib"
endif

pyso_LTLIBRARIES = nfs_clib.la

nfs_clib_la_CFLAGS = $(PYTHON_CFLAGS) -I$(top_srcdir)/c_binding/include
nfs_clib_la_SOURCES = nfs_clib.c
nfs_clib_la_LDFLAGS = $(PYTHON_LIBS) \
               -module -avoid-version -export-symbols-regex \
               $(_PY_CLIB_INIT_NAME)
nfs_clib_la_LIBADD = $(top_builddir)/c_binding/libstoragemgmt.la

dist_bin_SCRIPTS= nfs_lsmplugin
EXTRA_DIST= nfs_lsmplugin.in

endif
