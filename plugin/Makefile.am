SUBDIRS=simc megaraid hpsa arcconf nfs local ontap nstor smispy

plugindir = $(pythondir)/lsm/plugin

plugin_PYTHON= __init__.py

simdir = $(plugindir)/sim
sim_PYTHON = \
	sim/__init__.py \
	sim/simulator.py \
	sim/simarray.py

targetddir = $(plugindir)/targetd
targetd_PYTHON = \
	targetd/__init__.py \
	targetd/targetd.py

dist_bin_SCRIPTS= \
	sim/sim_lsmplugin \
	targetd/targetd_lsmplugin

EXTRA_DIST = \
	sim/sim_lsmplugin.in \
	targetd/targetd_lsmplugin.in
