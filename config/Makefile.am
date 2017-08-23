lsmconfdir=$(sysconfdir)/lsm
lsmconf_DATA=lsmd.conf

EXTRA_DIST= lsmd.conf pluginconf.d/sim.conf pluginconf.d/nfs.conf

pluginconfdir=$(sysconfdir)/lsm/pluginconf.d

pluginconf_DATA=pluginconf.d/sim.conf pluginconf.d/nfs.conf

if WITH_MEGARAID
pluginconf_DATA += pluginconf.d/megaraid.conf
EXTRA_DIST += pluginconf.d/megaraid.conf
endif

if WITH_HPSA
pluginconf_DATA += pluginconf.d/hpsa.conf
EXTRA_DIST += pluginconf.d/hpsa.conf
endif

if WITH_LOCAL
pluginconf_DATA += pluginconf.d/local.conf
EXTRA_DIST += pluginconf.d/local.conf
endif

if WITH_ARCCONF
pluginconf_DATA += pluginconf.d/arcconf.conf
EXTRA_DIST += pluginconf.d/arcconf.conf
endif
