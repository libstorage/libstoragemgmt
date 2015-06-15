#!/bin/bash

# Copyright (C) 2011-2014 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: tasleson
#
# Unit test case driver

# Make sure these are available in the envirnoment before we start lsmd
export G_SLICE=always-malloc
export G_DEBUG=gc-friendly
export CK_DEFAULT_TIMEOUT=600
export CK_FORK=no

rundir=$RANDOM
base=/tmp/$rundir

LSMD_PID=65535

export LSM_TEST_RUNDIR=$rundir
export LSM_UDS_PATH=$base/lsm/ipc/

LSMD_TMP_LOG_FILE="$base/lsmd.log"

cleanup() {
    #Clean up the daemon if it is running
    if [ $LSMD_PID -ne 65535 ]
    then
        kill -s KILL $LSMD_PID
    fi

    cat $LSMD_TMP_LOG_FILE
    if [ -e $LSM_UDS_PATH ]
    then
        rm -rf     $base
    fi

    if [ -e $rootdir/_build ]
    then
        rm $lsm_py_folder/lsm/plugin
        rm $lsm_py_folder/lsm/lsmcli
        chmod -w $lsm_py_folder/lsm
    fi
}

good() {
    echo "executing: $1"
    eval $1
    ec=$?
    if [ $ec -ne 0 ]; then
        echo "Fail exit[$ec]: $1"
        cleanup
        exit 1
    fi
}

# Add a signal handler to clean-up
trap "cleanup; exit 1" INT

# Unset these as they can cause the test case to fail
# specifically the password one, but remove both.
unset LSMCLI_PASSWORD
unset LSMCLI_URI

#Put us in a consistent spot
cd "$(dirname "$0")"

#Get base root directory
testdir=`pwd`
rootdir=${testdir%/*}

#Are we running within distcheck?
c_unit=$rootdir/test/tester
LSMD_DAEMON=$rootdir/daemon/lsmd
shared_libs=$rootdir/c_binding/.libs/
bin_plugin=$rootdir/plugin/simc/.libs/
lsm_py_folder=$rootdir/python_binding
lsm_plugin_py_folder=$rootdir/plugin
lsmcli_py_folder=$rootdir/tools/lsmcli

if [ -e $rootdir/_build ]
then
    c_unit=$rootdir/_build/test/tester
    LSMD_DAEMON=$rootdir/_build/daemon/lsmd
    shared_libs=$rootdir/_build/c_binding/.libs/
    bin_plugin=$rootdir/_build/plugin/simc/.libs/
    # In distcheck, all folder is read only(except _build and _inst).
    # which prevent us from linking plugin and lsmcli into python/lsm folder.
    chmod +w $rootdir/python_binding/lsm
fi

#With a distcheck you cannot muck with the source file system, so we will copy
#plugins somewhere else.
plugins=$base/plugins

#Export needed vars
export PYTHONPATH=$lsm_py_folder
export LD_LIBRARY_PATH=$base/lib
export LSM_SIM_DATA="$base/lsm_sim_data"

echo "testdir= $testdir"
echo "rootdir= $rootdir"
echo "c_unit=  $c_unit"

#Create the directory for the unix domain sockets
good "mkdir -p $LSM_UDS_PATH"
good "mkdir -p $plugins"
good "mkdir -p $LD_LIBRARY_PATH"

#Copy shared libraries
good "cp $shared_libs/*.so.* $LD_LIBRARY_PATH"

#Link plugin folder as python/lsm/plugin folder
if [ ! -L "$lsm_py_folder/lsm/plugin" ];then
    good "ln -s $lsm_plugin_py_folder $lsm_py_folder/lsm/"
fi

#Link lsmcli folder as python/lsm/lsmcli folder
if [ ! -L "$lsm_py_folder/lsm/lsmcli" ];then
    good "ln -s $lsmcli_py_folder $lsm_py_folder/lsm/"
fi

#Copy plugins to one directory.
good "find $rootdir/ \( ! -regex '.*/\..*' \) -type f -name \*_lsmplugin -exec cp {} $plugins \;"

#Copy the actual binary, not the shell script pointing to binary otherwise
#valgrind does not work.
good "cp $bin_plugin/*_lsmplugin $plugins"
good "ls -lh $plugins"

#Check to make sure that constants are correct
good "perl ../tools/utility/check_const.pl"

#Start daemon
$LSMD_DAEMON \
    --plugindir $plugins \
    --socketdir $LSM_UDS_PATH \
    -d >$LSMD_TMP_LOG_FILE &

# Let the daemon get settled before running the tests
sleep 2

LSMD_PID=$(ps aux | grep $LSM_UDS_PATH | grep -v grep |  awk '{print $2}')

#Run C unit test
if [ -z "$LSM_VALGRIND" ]; then
    good "$c_unit"
else
    good "valgrind --leak-check=full --show-reachable=no --log-file=/tmp/leaking_client $rootdir/test/.libs/tester"
fi

#Run cmdline against the simulator if we are not checking for leaks
if [ -z "$LSM_VALGRIND" ]; then
    export LSMCLI_URI='sim://'
    good "$rootdir/test/cmdtest.py -c $plugins/sim_lsmplugin"
    good "$rootdir/test/cmdtest.py -c $rootdir/tools/lsmcli/lsmcli"

    #Run the plug-in test against the python simulator
    good "$rootdir/test/plugin_test.py -v --uri sim://"
fi

#Run the plug-in test against the C simulator"
good "$rootdir/test/plugin_test.py -v --uri simc://"

#Pretend we were never here
cleanup
