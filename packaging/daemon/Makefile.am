
if HAVE_SYSTEMD

systemdsystemunit_DATA = libstoragemgmt.service

if WITH_REST_API
systemdsystemunit_DATA += libstoragemgmt-rest.service
endif

endif

EXTRA_DIST = libstoragemgmt.service lsm-tmpfiles.conf libstoragemgmtd
EXTRA_DIST += libstoragemgmt-rest.service
