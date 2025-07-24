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
        echo "Dumping daemon output on error"
        cat /tmp/lsmd_log.txt
        exit 1
    fi
}

# Make sure service is running
if ! pgrep -x "lsmd" > /dev/null; then
    echo "Starting the lsmd daemon"
    /usr/bin/lsmd -d -v > /tmp/lsmd_log.txt 2>&1 &
    sleep 5
fi

export LSMCLI_URI="sim://"
_good python3 cmdtest.py.in -c /usr/bin/lsmcli

export LSM_TEST_URI="sim://"
_good python3 plugin_test.py.in

