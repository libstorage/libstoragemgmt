#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Tests the lsmcli bash completion against the simulator plugin.  Where
# test_completion.sh pins the behaviour of the completion code with a stub,
# this checks the completion code against the real CLI, so that a change to
# the terse output of 'lsmcli list', or to a list type name, is caught.
#
# Neither a daemon nor root is needed: the hidden --fork_plugin option starts
# the plugin over a socket pair.  The completion script runs 'lsmcli' with no
# arguments of its own, so a wrapper first in PATH supplies them.

set -u

# Automake's exit code for a skipped test.
SKIP=77

# Field separator asked of 'lsmcli -t', matching the one the completion
# script uses.
SEP=$'\x1f'

srcdir=${srcdir:-$(dirname "$0")}
top_srcdir="${srcdir}/../.."
top_builddir="$(pwd)/../.."

completion_script="${srcdir}/lsmcli"
lsmcli_exe="${top_builddir}/tools/lsmcli/lsmcli"
sim_plugin="${top_builddir}/plugin/sim_plugin/sim_lsmplugin"
clib_so="${top_builddir}/python_binding/lsm/.libs/_clib.so"
c_lib_dir="${top_builddir}/c_binding/.libs"

# Everything here is built, and the simulator and the Python bindings are both
# optional, so skip rather than fail when the build did not produce them.
for required in "${completion_script}" "${lsmcli_exe}" "${sim_plugin}" \
                "${clib_so}" ; do
    if [[ ! -e ${required} ]] ; then
        echo "Skipping: ${required} not built"
        exit ${SKIP}
    fi
done

if ! command -v python3 > /dev/null ; then
    echo "Skipping: python3 not available"
    exit ${SKIP}
fi

stage_dir=$(mktemp -d) || exit 1
trap 'rm -rf "${stage_dir}"' EXIT

# Lay out an importable 'lsm' module from the build and source trees.  The
# generated files, eg. version.py, only exist in the build tree and have to
# land on top of their source tree namesakes.  'install' rather than 'cp'
# because distcheck makes the source tree read only, and cp would carry that
# over and then fail to overwrite.
mkdir -p "${stage_dir}/lsm/lsmcli" "${stage_dir}/lsm/plugin/sim_plugin" \
         "${stage_dir}/data" || exit 1
install -m 755 "${clib_so}" "${stage_dir}/lsm/_clib.so" || exit 1
install -m 644 "${top_srcdir}"/python_binding/lsm/*.py \
    "${stage_dir}/lsm/" || exit 1
install -m 644 "${top_builddir}"/python_binding/lsm/*.py \
    "${stage_dir}/lsm/" || exit 1
install -m 644 "${top_srcdir}"/tools/lsmcli/*.py \
    "${stage_dir}/lsm/lsmcli/" || exit 1
install -m 644 "${top_srcdir}"/plugin/sim_plugin/*.py \
    "${stage_dir}/lsm/plugin/sim_plugin/" || exit 1

export PYTHONPATH="${stage_dir}:${stage_dir}/lsm/plugin"
export LD_LIBRARY_PATH="${c_lib_dir}"
export LSM_SIM_DATA="${stage_dir}/data/lsm_sim_data"
# Simulated operations should return immediately rather than as a job.
export LSM_SIM_TIME=0

# The wrapper the completion script, and this test, invoke as 'lsmcli'.
cat > "${stage_dir}/lsmcli" <<EOF
#!/usr/bin/env bash
exec "$(readlink -f "${lsmcli_exe}")" \\
    --fork_plugin "$(readlink -f "${sim_plugin}")" -u sim:// "\$@"
EOF
chmod +x "${stage_dir}/lsmcli" || exit 1

export PATH="${stage_dir}:${PATH}"
export LSMCLI_AUTO_COMPLETE_VALUE=1
unset LSMCLI_PASSWORD

if ! lsmcli list --type systems -t"${SEP}" > /dev/null 2>&1 ; then
    echo "Skipping: cannot talk to the simulator plugin"
    lsmcli list --type systems -t"${SEP}"
    exit ${SKIP}
fi

# shellcheck source=tools/bash_completion/lsmcli
source "${completion_script}"

failures=0

function fail()
{
    echo "FAIL: $*"
    failures=$(( failures + 1 ))
}

# All of the output of a 'lsmcli list', read to the end.
#
# Nothing here may stop reading lsmcli early. Closing the pipe under it
# kills it with EPIPE part way through talking to the plugin, which both
# litters the log with BrokenPipeError and can leave the simulator state
# file inconsistent, so a later list returns a record that is not there.
# $1 Value for lsmcli list --type
# $@ Any remaining arguments are passed to lsmcli as-is
function list_records()
{
    local list_type="$1"
    shift
    lsmcli list --type "${list_type}" -t"${SEP}" "$@"
}

# Echo one column of every record, one per line.
# $1 Records, as returned by list_records
# $2 Column to echo
function column_of()
{
    local records="$1"
    local column="$2"
    local -a fields

    [[ -z ${records} ]] && return 0
    while IFS="${SEP}" read -r -a fields ; do
        echo "${fields[column-1]:-}"
    done <<< "${records}"
}

# Run _lsm for a command line and leave the result in COMPREPLY.
# $1 The words already on the command line
# $2 The word being completed
function complete_for()
{
    local -a words
    read -r -a words <<< "$1"
    COMP_WORDS=("${words[@]}" "$2")
    COMP_CWORD=${#words[@]}
    COMPREPLY=()
    _lsm
}

function reply_contains()
{
    local wanted="$1"
    local entry
    for entry in ${COMPREPLY[*]+"${COMPREPLY[@]}"} ; do
        [[ ${entry} == "${wanted}" ]] && return 0
    done
    return 1
}

# Check that completing an option offers exactly the identifiers a list
# returns. Both sides come from one snapshot, so a list taken a moment later
# cannot disagree with what the completion saw.
# $1 The option to complete after
# $2 Value for lsmcli list --type
# $3 A value that must be among the offered ones
function check_option()
{
    local option="$1"
    local list_type="$2"
    local wanted="$3"
    local records expected actual

    records=$(list_records "${list_type}")
    if [[ -z ${records} ]] ; then
        fail "${option}: 'lsmcli list --type ${list_type}' returned nothing"
        return
    fi
    expected=$(column_of "${records}" 1)

    complete_for "lsmcli x ${option}" ""
    actual=$(printf '%s\n' ${COMPREPLY[*]+"${COMPREPLY[@]}"})

    if [[ ${actual} != "${expected}" ]] ; then
        fail "${option}: offered [${actual//$'\n'/, }]," \
             "'lsmcli list --type ${list_type}' has [${expected//$'\n'/, }]"
        return
    fi
    if ! reply_contains "${wanted}" ; then
        fail "${option}: '${wanted}' missing from: ${COMPREPLY[*]}"
    fi
}

# Echo the value of a column of the first record whose other column matches.
# $1 Value for lsmcli list --type
# $2 Column to match on
# $3 Value to match
# $4 Column to echo
# $@ Any remaining arguments are passed to lsmcli as-is
function value_where()
{
    local list_type="$1"
    local match_column="$2"
    local match_value="$3"
    local want_column="$4"
    local records found=""
    local -a fields
    shift 4

    records=$(list_records "${list_type}" "$@")
    [[ -z ${records} ]] && return 1
    # Read every record even once a match is found, see list_records.
    while IFS="${SEP}" read -r -a fields ; do
        if [[ -z ${found} && ${fields[match_column-1]:-} == "${match_value}" ]]
        then
            found="${fields[want_column-1]:-}"
        fi
    done <<< "${records}"

    [[ -z ${found} ]] && return 1
    echo "${found}"
}

# Echo the first column of the first record of a list.
# $1 Value for lsmcli list --type
function first_value()
{
    column_of "$(list_records "$1")" 1 | head -n 1
}

# A fresh simulator has systems, pools, disks and target ports but no
# volumes, access groups or file systems, so make the objects we need.
pool_id=$(first_value pools)
if [[ -z ${pool_id} ]] ; then
    echo "Skipping: simulator returned no pools"
    exit ${SKIP}
fi

vol_name="lsm_test_vol"
ag_name="lsm_test_ag"
ag_init="iqn.1994-05.com.domain:01.89bd0123"
fs_name="lsm_test_fs"
snap_name="lsm_test_snap"
export_path="/lsm_test_export"

if ! lsmcli volume-create --name "${vol_name}" --size 100MiB \
        --pool "${pool_id}" > /dev/null ; then
    fail "could not create a volume to complete against"
fi
if ! lsmcli access-group-create --name "${ag_name}" --init "${ag_init}" \
        --sys sim-01 > /dev/null ; then
    fail "could not create an access group to complete against"
fi
if ! lsmcli fs-create --name "${fs_name}" --size 100MiB \
        --pool "${pool_id}" > /dev/null ; then
    fail "could not create a file system to complete against"
fi

vol_id=$(value_where volumes 2 "${vol_name}" 1)
ag_id=$(value_where access_groups 2 "${ag_name}" 1)
fs_id=$(value_where fs 2 "${fs_name}" 1)

if ! lsmcli fs-snap-create --name "${snap_name}" --fs "${fs_id}" \
        > /dev/null ; then
    fail "could not create a snapshot to complete against"
fi
snap_id=$(value_where snapshots 2 "${snap_name}" 1 --fs "${fs_id}")

if ! lsmcli fs-export --fs "${fs_id}" --exportpath "${export_path}" \
        --rw-host 192.0.2.1 > /dev/null ; then
    fail "could not create an export to complete against"
fi
export_id=$(value_where exports 3 "${export_path}" 1)

check_option "--sys" systems "sim-01"
check_option "--pool" pools "${pool_id}"
check_option "--vol" volumes "${vol_id}"
check_option "--src-vol" volumes "${vol_id}"
check_option "--dst-vol" volumes "${vol_id}"
check_option "--disk" disks "$(first_value disks)"
check_option "--ag" access_groups "${ag_id}"
check_option "--fs" fs "${fs_id}"
check_option "--src-fs" fs "${fs_id}"
check_option "--tgt" target_ports "$(first_value target_ports)"
check_option "--export" exports "${export_id}"
check_option "--nfs-export" exports "${export_id}"

# --init takes the third column, the initiator, not the first.
complete_for "lsmcli x --init" ""
if ! reply_contains "${ag_init}" ; then
    fail "--init: '${ag_init}' missing from: ${COMPREPLY[*]}"
fi

# With an access group on the command line the initiators are filtered to it.
complete_for "lsmcli x --ag ${ag_id} --init" ""
if [[ ${#COMPREPLY[@]} -ne 1 ]] || ! reply_contains "${ag_init}" ; then
    fail "--init filtered by --ag ${ag_id} gave: ${COMPREPLY[*]}"
fi

# --snap needs a --fs on the command line, and offers nothing without one.
complete_for "lsmcli x --fs ${fs_id} --snap" ""
if ! reply_contains "${snap_id}" ; then
    fail "--snap: '${snap_id}' missing from: ${COMPREPLY[*]}"
fi
complete_for "lsmcli x --snap" ""
if [[ ${#COMPREPLY[@]} -ne 0 ]] ; then
    fail "--snap without a --fs offered: ${COMPREPLY[*]}"
fi

# A prefix narrows the result, and a prefix matching nothing gives nothing.
complete_for "lsmcli x --vol" "${vol_id:0:3}"
if ! reply_contains "${vol_id}" ; then
    fail "--vol with prefix '${vol_id:0:3}' gave: ${COMPREPLY[*]}"
fi
complete_for "lsmcli x --vol" "no_such_volume"
if [[ ${#COMPREPLY[@]} -ne 0 ]] ; then
    fail "--vol with a non-matching prefix offered: ${COMPREPLY[*]}"
fi

# --auth-type is the one lookup whose output is a single line, with every
# type a field of it, so offer every field rather than a column.
auth_records=$(list_records nfs_client_auth)
auth_expected=""
if [[ -n ${auth_records} ]] ; then
    IFS="${SEP}" read -r -a auth_types <<< "${auth_records}"
    auth_expected=$(printf '%s\n' "${auth_types[@]}")
fi
complete_for "lsmcli x --auth-type" ""
auth_actual=$(printf '%s\n' ${COMPREPLY[*]+"${COMPREPLY[@]}"})
if [[ -z ${auth_expected} || ${auth_actual} != "${auth_expected}" ]] ; then
    fail "--auth-type: offered [${auth_actual//$'\n'/, }]," \
         "'lsmcli list --type nfs_client_auth' has" \
         "[${auth_expected//$'\n'/, }]"
fi

if [[ ${failures} -ne 0 ]] ; then
    echo "${failures} simulator completion test(s) failed"
    exit 1
fi

echo "All simulator completion tests passed"
exit 0
