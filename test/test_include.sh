#!/bin/bash
# Copyright (C) 2015 Red Hat, Inc.
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
# Author: Gris Ge <fge@redhat.com>
#

_LSM_C_PLUGINS="simc/simc_lsmplugin"


LSM_TEST_INSTALL_PY_PLUGINS_ONLY=1
LSM_TEST_INSTALL_C_PLUGINS_ONLY=2
LSM_TEST_INSTALL_ALL_PLUGINS=3

LSM_TEST_WITHOUT_MEM_CHECK=0
LSM_TEST_WITH_MEM_CHECK=1

LSM_TEST_SIM_URI="sim://"
LSM_TEST_SIMC_URI="simc://"

LSM_TEST_KEEP_INSTALL_BASE=1

export LSM_TEST_DST_DIR=""
export LSM_TEST_PLUGIN_DIR=""
export LSM_TEST_BIN_DIR=""
export LSM_TEST_PY_MODULE_DIR=""
export LSM_TEST_C_LIB_DIR=""
export LSM_TEST_LOG_DIR=""
export LSM_SIM_TIME=0.1

LSM_TEST_MEM_LEAK_ERROR_CODE=99
LSM_TEST_MEM_LEAK_LOG_FILE_PREFIX=""
LSM_TEST_LSMD_PID=65535

VALGRIND_OPTIONS="
    --quiet --leak-check=full --show-reachable=no --show-possibly-lost=no
    --trace-children=yes --error-exitcode=$LSM_TEST_MEM_LEAK_ERROR_CODE"

VALGRIND_OPTIONS_3_9="${VALGRIND_OPTIONS} --errors-for-leak-kinds=definite "

VALGRIND_VERSION="$(valgrind --version 2>/dev/null)"
VALGRIND_VERSION_MAJOR="$(echo $VALGRIND_VERSION|
    sed -ne 's/valgrind-\([0-9]\+\)\..*/\1/p')"
VALGRIND_VERSION_MINOR="$(echo $VALGRIND_VERSION|
    sed -ne 's/valgrind-[0-9]\+.\([0-9]\+\)\..*/\1/p')"

if [ "CHK$VALGRIND_VERSION_MAJOR" == "CHK" ] || \
   [ "CHK$VALGRIND_VERSION_MINOR" == "CHK" ];then
    # Unknown version
    VALGRIND_OPTIONS=$VALGRIND_OPTIONS_3_9
fi

if [ $VALGRIND_VERSION_MAJOR -gt 3 ];then
    VALGRIND_OPTIONS=$VALGRIND_OPTIONS_3_9
fi

if [ $VALGRIND_VERSION_MAJOR -eq 3 ] && [ $VALGRIND_VERSION_MINOR -ge 9 ];then
    VALGRIND_OPTIONS=$VALGRIND_OPTIONS_3_9
fi

LIBTOOL_CMD_NO_WARN="libtool --warning=none"
if [ "CHK$($LIBTOOL_CMD_NO_WARN 2>&1 | grep 'unrecognized option')" \
     != "CHK" ];then
    # Dirty hack for old system like Ubuntu 12.04
    # On new libtool, '-no-warn' does not works.
    LIBTOOL_CMD_NO_WARN="libtool --no-warn"
fi

if [ "CHK$($LIBTOOL_CMD_NO_WARN 2>&1 | grep 'unrecognized option')" \
     != "CHK" ];then
    # Dirty hack for old system like RHEL6 which don't even have --no-warn
    # option.
    LIBTOOL_CMD_NO_WARN="libtool"
fi

function lsm_test_cleanup
{
    local keep_base="$1";

    #Clean up the daemon if it is running
    if [ "CHK$LSM_TEST_LSMD_PID" != "CHK" ] &&
       [ "CHK$LSM_TEST_LSMD_PID" != "CHK65535" ]; then
        kill -s KILL $LSM_TEST_LSMD_PID
    fi

    if [ -e $LSM_UDS_PATH ]; then
        if [ "CHK$keep_base" != "CHK${LSM_TEST_KEEP_INSTALL_BASE}" ]; then
            chmod +w -R ${LSM_TEST_DST_DIR}
            rm -rf     ${LSM_TEST_DST_DIR}
        else
            echo "Base folder ${LSM_TEST_DST_DIR} is kept for investigation"
        fi
    fi
}

function lsm_test_dump_log
{
    echo "============ Dumping log BEGIN ====================="
    for x in $LSM_TEST_LOG_DIR/*;do
        if [ -e $x ] && [ $(cat $x|wc -l) -gt 0 ];then
            echo ======== $x BEGIN ==========
            cat $x
            echo ======== $x END   ==========
        fi
    done
    echo "============ Dumping log END   ====================="
}

function _good
{
    echo "executing: $@"
    eval "$@"
    local rc=$?
    if [ $rc -ne 0 ]; then
        # We don't do clean on error in case we need to investigate.
        if [ $rc -eq $LSM_TEST_MEM_LEAK_ERROR_CODE ];then
            echo "Found memory leak"
        else
            echo "Fail exit[$rc]: $@"
        fi
        lsm_test_dump_log
        echo "Base folder is '$LSM_TEST_DST_DIR', please investigate"
        lsm_test_cleanup $LSM_TEST_KEEP_INSTALL_BASE
        exit 1
    fi
}

function _fail
{
    lsm_test_dump_log
    echo "$@"
    lsm_test_cleanup $LSM_TEST_KEEP_INSTALL_BASE;
    exit 1
}

#
# Usage:
#   Install required files into certain folder for test purpose.
# Argument:
#   $dst_dir
#       Destination folder.
#   $build_dir
#       Build folder.
#   $src_dir
#       Source code folder.
#   $plugin_type
#       $LSM_TEST_INSTALL_PY_PLUGINS_ONLY
#           Install python plugins only.
#       $LSM_TEST_INSTALL_C_PLUGINS_ONLY
#           Install c plugins only.
#       $LSM_TEST_INSTALL_ALL_PLUGINS
#           Install all plugins.
function lsm_test_base_install
{
    local dst_dir="$1";
    local build_dir="$2";
    local src_dir="$3";
    local plugin_type="$4";

    if [ "CHK$dst_dir" == "CHK" ]   ||
       [ "CHK$build_dir" == "CHK" ] ||
       [ "CHK$src_dir" == "CHK" ]   ||
       [ "CHK$plugin_type" == "CHK" ];then
        _fail "lsm_test_base_install(): Invalid argument"
    fi

    LSM_TEST_DST_DIR="${dst_dir}"
    LSM_TEST_PLUGIN_DIR="${dst_dir}/plugins/"
    LSM_TEST_BIN_DIR="${dst_dir}/bin/"
    LSM_TEST_PY_MODULE_DIR="${dst_dir}/python_modules/"
    LSM_TEST_C_LIB_DIR="${dst_dir}/c_libs/"
    LSM_TEST_LOG_DIR="${dst_dir}/logs/"
    LSM_TEST_MEM_LEAK_LOG_FILE_PREFIX="${LSM_TEST_LOG_DIR}/mem_leak_"

    export PYTHONPATH="${LSM_TEST_PY_MODULE_DIR}"
    export LD_LIBRARY_PATH="${LSM_TEST_C_LIB_DIR}"
    export LSM_SIM_DATA="${LSM_TEST_DST_DIR}/plugin_data/lsm_sim_data"
    export LSM_UDS_PATH="${dst_dir}/ipc"
    export LSM_TEST_RUNDIR="${LSM_TEST_DST_DIR}/plugin_data/"

    echo "==================================="
    echo "PYTHONPATH=${PYTHONPATH}"
    echo "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"
    echo "LSM_SIM_DATA=${LSM_SIM_DATA}"
    echo "LSM_UDS_PATH=${LSM_UDS_PATH}"
    echo "LSM_TEST_RUNDIR=${LSM_TEST_RUNDIR}"
    echo "==================================="

    _good mkdir "${LSM_TEST_DST_DIR}"
    _good mkdir "${LSM_TEST_PLUGIN_DIR}"
    _good mkdir "${LSM_TEST_BIN_DIR}"
    _good mkdir "${LSM_TEST_PY_MODULE_DIR}"
    _good mkdir "${LSM_TEST_C_LIB_DIR}"
    _good mkdir "${LSM_TEST_LOG_DIR}"
    _good mkdir "${LSM_UDS_PATH}"
    _good mkdir "$LSM_TEST_PY_MODULE_DIR/lsm"
    _good mkdir "$LSM_TEST_PY_MODULE_DIR/lsm/external"
    _good mkdir "$LSM_TEST_PY_MODULE_DIR/lsm/plugin"
    _good mkdir "$LSM_TEST_PY_MODULE_DIR/lsm/lsmcli"
    _good mkdir "$LSM_TEST_RUNDIR"

    # Make sure LSM_UDS_PATH is gloable writeable in case 'sudo make check'
    _good chmod 0777 ${LSM_UDS_PATH}
    # Make sure LSM_TEST_RUNDIR is gloable writeable in case 'sudo make check'
    _good chmod 0777 ${LSM_TEST_RUNDIR}
    # Make suer log folder is gloable writeable in case 'sudo make check'
    _good chmod 0777 ${LSM_TEST_LOG_DIR}


    _good $LIBTOOL_CMD_NO_WARN --mode install \
        install "${build_dir}/daemon/lsmd" "$LSM_TEST_BIN_DIR"
    _good $LIBTOOL_CMD_NO_WARN --mode install \
        install "${build_dir}/c_binding/libstoragemgmt.la" "$LSM_TEST_C_LIB_DIR"

    # libtool 'install' mode does not works against python C extension,
    # use manual copy instead
    _good cp "${build_dir}/python_binding/lsm/.libs/_clib.so" \
        "${LSM_TEST_PY_MODULE_DIR}/lsm/_clib.so"
    _good chrpath -d "${LSM_TEST_PY_MODULE_DIR}/lsm/_clib.so"

    _good find "${src_dir}/python_binding/lsm/" -maxdepth 1 \
        -type f -name '*.py' \
        -exec install -D "{}" "$LSM_TEST_PY_MODULE_DIR/lsm/" \\\;

    _good find "${src_dir}/python_binding/lsm/external/" -maxdepth 1 \
        -type f -name '*.py' \
        -exec install -D "{}" "$LSM_TEST_PY_MODULE_DIR/lsm/external" \\\;

    _good find "${src_dir}/tools/lsmcli/" -maxdepth 1 -type f -name '*.py' \
        -exec install -D "{}" "$LSM_TEST_PY_MODULE_DIR/lsm/lsmcli/" \\\;

    _good install -D "${src_dir}/tools/lsmcli/lsmcli" \
        "${LSM_TEST_BIN_DIR}/lsmcli"
    _good $LIBTOOL_CMD_NO_WARN --mode install \
        install "${build_dir}/test/tester" "${LSM_TEST_BIN_DIR}/tester"
    _good install "${src_dir}/test/plugin_test.py" \
        "${LSM_TEST_BIN_DIR}/plugin_test.py"
    _good install "${src_dir}/test/cmdtest.py" \
        "${LSM_TEST_BIN_DIR}/cmdtest.py"

    local legal_plugin_type=0

    if [ "CHK${plugin_type}" == "CHK${LSM_TEST_INSTALL_PY_PLUGINS_ONLY}" ] || \
       [ "CHK${plugin_type}" == "CHK${LSM_TEST_INSTALL_ALL_PLUGINS}" ];then
        _good cp -av ${src_dir}/plugin ${LSM_TEST_PY_MODULE_DIR}/lsm/
        _good find ${src_dir}/plugin '\( ! -regex ".*/\..*" \)' \
            -name \*_lsmplugin \
            -exec install -D {} ${LSM_TEST_PLUGIN_DIR} \\\;
        # When source folder is the build folder, above command might also
        # copied C plugin(libtool wrapper file).
        local tmp_plugin
        for tmp_plugin in ${LSM_TEST_PLUGIN_DIR}/*; do
            if [ "CHK$(file -b $tmp_plugin |grep -i Python)" == "CHK" ];then
                _good rm $tmp_plugin
            fi
        done

        legal_plugin_type=1
    fi

    if [ "CHK${plugin_type}" == "CHK${LSM_TEST_INSTALL_C_PLUGINS_ONLY}" ] || \
       [ "CHK${plugin_type}" == "CHK${LSM_TEST_INSTALL_ALL_PLUGINS}" ];then
        local c_plugin
        for c_plugin in $_LSM_C_PLUGINS; do
            _good $LIBTOOL_CMD_NO_WARN --mode install \
                install -D "${build_dir}/plugin/$c_plugin" $LSM_TEST_PLUGIN_DIR
        done
        legal_plugin_type=1
    fi

    if [ $legal_plugin_type -eq 0 ];then
        _fail "lsm_test_base_install(): Invalid argument"
    fi
    echo "Installed plugins"
    echo "==================================="
    ls -l ${LSM_TEST_PLUGIN_DIR}
    echo "==================================="
}

function lsm_test_lsmd_start
{
    local with_mem_check="$1"

    local cmd="${LSM_TEST_BIN_DIR}/lsmd
        --plugindir=${LSM_TEST_PLUGIN_DIR}
        --socketdir=${LSM_UDS_PATH}
        -d -v > ${LSM_TEST_LOG_DIR}/lsmd.log &"

    if [ "CHK${with_mem_check}" == "CHK${LSM_TEST_WITH_MEM_CHECK}" ];then
        # Unset the LSM_VALGRIND via env command to skip duplicate memory check
        # in lsmd.
        if [ "CHK${VALGRIND_VERSION}" == "CHK" ];then
            _fail "valgrind not found in \$PATH for memory check"
        fi

        cmd="env --unset=LSM_VALGRIND valgrind ${VALGRIND_OPTIONS}
             --log-file=${LSM_TEST_MEM_LEAK_LOG_FILE_PREFIX}_lsmd_%p ${cmd}"
    fi
    #Start daemon
    echo $cmd
    eval $cmd
    sleep 2
    LSM_TEST_LSMD_PID=$(ps aux | grep $LSM_UDS_PATH | \
                        grep -v grep |  awk '{print $2}')
    if [ "CHK${LSM_TEST_LSMD_PID}" == "CHK" ];then
        _fail "Failed to start lsmd daemon"
    fi
}

function lsm_test_check_memory_leak
{
    for x in ${LSM_TEST_MEM_LEAK_LOG_FILE_PREFIX}*; do
        if [ -e $x ] && \
           [ $(wc -l $x|perl -ne 'print $1 if /^([0-9]+)/') -gt 0 ];then
            lsm_test_dump_log
            _fail "Found memory leak"
        fi
    done
}

function lsm_test_c_unit_test_run
{
    local with_mem_check="$1"
    local plugin_type="$2"
    local cmd="${LSM_TEST_BIN_DIR}/tester"

    if [ "CHK$plugin_type" == "CHK$LSM_TEST_SIMC_URI" ];then
        cmd="${cmd} use_simc"
    fi

    if [ "CHK${with_mem_check}" == "CHK${LSM_TEST_WITH_MEM_CHECK}" ];then
        cmd="valgrind ${VALGRIND_OPTIONS}
             --log-file=${LSM_TEST_MEM_LEAK_LOG_FILE_PREFIX}leaking_client $cmd"
    fi
    _good $cmd
}

function lsm_test_cmd_test_run
{
    local plugin_type="$1"

    if [ "CHK$plugin_type" == "CHK$LSM_TEST_SIMC_URI" ];then
        export LSMCLI_URI="simc://"
    elif [ "CHK$plugin_type" == "CHK$LSM_TEST_SIM_URI" ];then
        export LSMCLI_URI="sim://"
    else
        _fail 'lsm_test_cmd_test_run(): Got invalid argument'
    fi

    unset LSMCLI_PASSWORD

    _good $LSM_TEST_BIN_DIR/cmdtest.py -c $LSM_TEST_BIN_DIR/lsmcli
    # TODO(Gris Ge): Should we add running from plugin here.
}

function lsm_test_plugin_test_run
{
    export LSM_TEST_URI="$1";
    export LSM_TEST_PASSWORD="$2";
    if [ "CHK${LSM_TEST_PASSWORD}" == "CHK" ];then
        unset LSM_TEST_PASSWORD
    fi

    _good $LSM_TEST_BIN_DIR/plugin_test.py -v
}
