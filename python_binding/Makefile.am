
lsmdir = $(pythondir)/lsm
externaldir = $(lsmdir)/external

lsm_PYTHON = \
	lsm/__init__.py \
	lsm/_client.py \
	lsm/_common.py \
	lsm/_data.py \
	lsm/_transport.py \
	lsm/version.py \
	lsm/_iplugin.py \
	lsm/_pluginrunner.py

pyexec_LTLIBRARIES = lsm/_scsi.la
pyexecdir = $(pythondir)/lsm

lsm__scsi_la_CFLAGS = $(PYTHON2_CFLAGS) -I$(top_srcdir)/c_binding/include
lsm__scsi_la_SOURCES = lsm/_scsi.c
lsm__scsi_la_LDFLAGS = $(PYTHON2_LIBS) \
		       -module -avoid-version -export-symbols-regex init_scsi
lsm__scsi_la_LIBADD = $(top_builddir)/c_binding/libstoragemgmt.la

external_PYTHON = \
	lsm/external/__init__.py \
	lsm/external/xmltodict.py
