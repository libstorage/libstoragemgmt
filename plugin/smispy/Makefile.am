if WITH_SMISPY

plugindir = $(pythondir)/lsm/plugin
smispydir = $(plugindir)/smispy

smispy_PYTHON = \
	__init__.py \
	smis.py \
	utils.py \
	smis_common.py \
	dmtf.py \
	smis_cap.py \
	smis_sys.py \
	smis_pool.py \
	smis_disk.py \
	smis_ag.py \
	smis_vol.py

dist_bin_SCRIPTS = smispy_lsmplugin
EXTRA_DIST = smispy_lsmplugin.in
endif
