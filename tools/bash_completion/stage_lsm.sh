#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Lays out an importable 'lsm' module, with lsm.lsmcli, from the build and
# source trees, for the completion tests, which run the real code of lsmcli
# but not through an installed one. The generated files, eg. version.py, only
# exist in the build tree and have to land on top of their source tree
# namesakes. 'install' rather than 'cp' because distcheck makes the source tree
# read only, and cp would carry that over and then fail to overwrite.
#
# Usage: stage_lsm.sh <source dir of the bash completion> <empty directory>
# Exits with 77, the code for a skipped test, when the Python bindings were not
# built. The environment to import it with is PYTHONPATH=<directory> and
# LD_LIBRARY_PATH=<top build directory>/c_binding/.libs.

set -u

srcdir=$1
stage_dir=$2
top_srcdir="${srcdir}/../.."
top_builddir="$(pwd)/../.."
clib_so="${top_builddir}/python_binding/lsm/.libs/_clib.so"

if [[ ! -e ${clib_so} ]] ; then
    echo "Skipping: ${clib_so} not built"
    exit 77
fi

mkdir -p "${stage_dir}/lsm/lsmcli" || exit 1
install -m 755 "${clib_so}" "${stage_dir}/lsm/_clib.so" || exit 1
install -m 644 "${top_srcdir}"/python_binding/lsm/*.py \
    "${stage_dir}/lsm/" || exit 1
install -m 644 "${top_builddir}"/python_binding/lsm/*.py \
    "${stage_dir}/lsm/" || exit 1
install -m 644 "${top_srcdir}"/tools/lsmcli/*.py \
    "${stage_dir}/lsm/lsmcli/" || exit 1
