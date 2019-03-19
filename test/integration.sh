#!/usr/bin/bash

# We are wanting to execute the cmdtest and plugin test against
# installed packages using the simulator.  This test ensures
# that a packaged install is working.

function _good
{
    echo "executing: $@"
    eval "$@"
    local rc=$?
    if [ $rc -ne 0 ]; then
        exit 1
    fi
}

# Make sure service is running
systemctl start libstoragemgmt

export LSMCLI_URI="sim://"
_good python3 cmdtest.py.in -c /usr/bin/lsmcli

export LSM_TEST_URI="sim://"
_good python3 plugin_test.py.in
