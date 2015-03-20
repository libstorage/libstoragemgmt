lsmconfdir=$(sysconfdir)/lsm
lsmconf_DATA=lsmd.conf

EXTRA_DIST= lsmd.conf pluginconf.d/sim.conf

pluginconfdir=$(sysconfdir)/lsm/pluginconf.d

pluginconf_DATA=pluginconf.d/sim.conf

if WITH_MEGARAID
pluginconf_DATA += pluginconf.d/megaraid.conf
EXTRA_DIST += pluginconf.d/megaraid.conf
endif

if WITH_HPSA
pluginconf_DATA += pluginconf.d/hpsa.conf
EXTRA_DIST += pluginconf.d/hpsa.conf
endif
