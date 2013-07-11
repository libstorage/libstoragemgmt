
if HAVE_PARAMIKO
IBM_PLUG = v7k_lsmplugin
IBM_CLASS = lsm/ibmv7k.py
else
IBM_PLUG =
IBM_CLASS =
endif

dist_bin_SCRIPTS = sim_lsmplugin ontap_lsmplugin smispy_lsmplugin targetd_lsmplugin nstor_lsmplugin

lsmdir = $(pythondir)/lsm
externaldir = $(lsmdir)/external

lsm_PYTHON = lsm/__init__.py           \
                   lsm/client.py       \
                   lsm/cmdline.py      \
                   lsm/common.py       \
                   lsm/data.py         \
                   lsm/eseries.py      \
                   lsm/iplugin.py      \
                   lsm/na.py           \
                   lsm/ontap.py        \
                   lsm/pluginrunner.py \
                   lsm/simulator.py    \
                   lsm/smis.py         \
                   lsm/smisproxy.py    \
                   lsm/transport.py    \
                   lsm/version.py


dist_bin_SCRIPTS += $(IBM_PLUG)
lsm_PYTHON += $(IBM_CLASS)


external_PYTHON = lsm/external/__init__.py     \
                   lsm/external/daemon.py      \
                   lsm/external/enumeration.py \
                   lsm/external/xmltodict.py 
