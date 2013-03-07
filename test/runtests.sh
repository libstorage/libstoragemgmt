#!/bin/bash

# Copyright (C) 2011-2013 Red Hat, Inc.
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
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
#
# Author: tasleson
#
# Unit test case driver

rundir=$RANDOM
base=/tmp/$rundir

LSMD_PID=65535

export LSM_TEST_RUNDIR=$rundir
export LSM_UDS_PATH=$base/lsm/ipc/

cleanup() {

	#Clean up the daemon if it is running
	kill $LSMD_PID

	if [ -e $LSM_UDS_PATH ]
	then
		rm -rf 	$base
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

#Put us in a consistant spot
cd "$(dirname "$0")"

#Get base root directory
testdir=`pwd`
rootdir=${testdir%/*}

#Are we running within distcheck?
c_unit=$rootdir/test/tester
LSMD_DAEMON=$rootdir/src/lsmd
if [ -e $rootdir/_build ]
then
	c_unit=$rootdir/_build/test/tester
	LSMD_DAEMON=$rootdir/_build/src/lsmd
fi

#With a distcheck you cannot much with the source file system, so we will copy
#plugins somewhere else.
plugins=$base/plugins

echo "testdir= $testdir"
echo "rootdir= $rootdir"
echo "c_unit=  $c_unit"

#Create the directory for the unix domain sockets
good "mkdir -p $LSM_UDS_PATH"
good "mkdir -p $plugins"

#Copy plugins to one directory.
good "find $rootdir \( ! -regex '.*/\..*' \) -type f -name \*_lsmplugin -exec cp {} $plugins \;"

#Export needed vars
export PYTHONPATH=$rootdir/lsm
export LD_LIBRARY_PATH=$rootdir/src
export LSM_SIM_DATA="$base/lsm_sim_data"

#Start daemon
good "$LSMD_DAEMON --plugindir $plugins --socketdir $LSM_UDS_PATH" -v

LSMD_PID=`pidof lsmd`

#Run C unit test
export CK_DEFAULT_TIMEOUT=600
good "$c_unit"

#Run cmdline against the simulator
export LSMCLI_URI='sim://'
good "$rootdir/test/cmdtest.py -c $rootdir/tools/lsmclipy/lsmcli"

#Pretend we were never here
cleanup
