SUBDIRS=simc megaraid hpsa arcconf nfs local ontap nstor smispy targetd

plugindir = $(pythondir)/lsm/plugin

plugin_PYTHON= __init__.py

simdir = $(plugindir)/sim
sim_PYTHON = \
	sim/__init__.py \
	sim/simulator.py \
	sim/simarray.py

dist_bin_SCRIPTS= sim/sim_lsmplugin

EXTRA_DIST = sim/sim_lsmplugin.in
