#!/bin/bash
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Sets up the environment to run libstoragemgmt from the source/build tree
# without installing. This replicates what "make check" does via
# test_include.sh, but leaves a long-lived environment the developer can
# interact with.
#
# Usage:
#   source test/devel_env.sh     # sets env vars, starts daemon
#   lsmcli list --type systems   # use lsmcli normally
#   devel_env_stop               # tear down when done

# ---------------------------------------------------------------------------
# Guard: skip re-initialization if a daemon is already running
# ---------------------------------------------------------------------------
if [ -n "$DEVEL_BASE" ] && [ -n "$DEVEL_LSMD_PID" ] && \
        kill -0 "$DEVEL_LSMD_PID" 2>/dev/null; then
    echo "libstoragemgmt development environment already active (PID $DEVEL_LSMD_PID)."
    echo "Run 'devel_env_stop' first to tear it down."
    return 0 2>/dev/null || exit 0
fi

# ---------------------------------------------------------------------------
# Locate the source and build trees (script lives in test/)
# ---------------------------------------------------------------------------
_DEVEL_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_DEVEL_SRC_DIR="$(readlink -f "$_DEVEL_SCRIPT_DIR/..")"
_DEVEL_BUILD_DIR="$_DEVEL_SRC_DIR"  # in-tree build assumed

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
_devel_err=0
if [ ! -f "$_DEVEL_BUILD_DIR/c_binding/.libs/libstoragemgmt.so" ]; then
    echo "ERROR: libstoragemgmt.so not found in c_binding/.libs/"
    _devel_err=1
fi

if [ ! -f "$_DEVEL_BUILD_DIR/daemon/lsmd" ]; then
    echo "ERROR: lsmd daemon not found in daemon/"
    _devel_err=1
fi

_devel_clib_so=""
if [ -f "$_DEVEL_BUILD_DIR/python_binding/lsm/.libs/_clib.so" ]; then
    _devel_clib_so="$_DEVEL_BUILD_DIR/python_binding/lsm/.libs/_clib.so"
elif [ -f "$_DEVEL_BUILD_DIR/python_binding/lsm/.libs/_clib3.so" ]; then
    _devel_clib_so="$_DEVEL_BUILD_DIR/python_binding/lsm/.libs/_clib3.so"
else
    echo "ERROR: Python C extension (_clib.so / _clib3.so) not found."
    _devel_err=1
fi

if [ $_devel_err -ne 0 ]; then
    echo "       Run './configure && make' first."
    unset _devel_err _devel_clib_so _DEVEL_SCRIPT_DIR _DEVEL_SRC_DIR _DEVEL_BUILD_DIR
    return 1 2>/dev/null || exit 1
fi
unset _devel_err

# ---------------------------------------------------------------------------
# Detect working libtool invocation (matches test_include.sh logic)
# ---------------------------------------------------------------------------
_DEVEL_LIBTOOL="libtool --warning=none"
if $_DEVEL_LIBTOOL --version >/dev/null 2>&1; then
    : # good
elif libtool --no-warn --version >/dev/null 2>&1; then
    _DEVEL_LIBTOOL="libtool --no-warn"
else
    _DEVEL_LIBTOOL="libtool"
fi

# ---------------------------------------------------------------------------
# Create a temporary runtime directory tree
# ---------------------------------------------------------------------------
DEVEL_BASE="$(mktemp -d /tmp/lsm_devel_XXXXXXXXXX)" || {
    echo "ERROR: Failed to create temporary directory"
    unset _devel_err _devel_clib_so _DEVEL_SCRIPT_DIR _DEVEL_SRC_DIR _DEVEL_BUILD_DIR _DEVEL_LIBTOOL
    return 1 2>/dev/null || exit 1
}
mkdir -p "$DEVEL_BASE"/{bin,plugins,python_modules/lsm/plugin,python_modules/lsm/lsmcli,c_libs,logs,ipc,plugin_data,config/pluginconf.d}

chmod 0777 "$DEVEL_BASE/ipc"
chmod 0777 "$DEVEL_BASE/plugin_data"
chmod 0777 "$DEVEL_BASE/logs"

# ---------------------------------------------------------------------------
# Install the C library (via libtool so the .so symlinks are correct)
# ---------------------------------------------------------------------------
$_DEVEL_LIBTOOL --mode install \
    install "$_DEVEL_BUILD_DIR/c_binding/libstoragemgmt.la" \
    "$DEVEL_BASE/c_libs/" > /dev/null 2>&1

# ---------------------------------------------------------------------------
# Install the daemon (via libtool for consistent autotools handling)
# ---------------------------------------------------------------------------
$_DEVEL_LIBTOOL --mode install \
    install "$_DEVEL_BUILD_DIR/daemon/lsmd" "$DEVEL_BASE/bin/lsmd" > /dev/null 2>&1

# ---------------------------------------------------------------------------
# Install the Python C extension
# ---------------------------------------------------------------------------
cp "$_devel_clib_so" "$DEVEL_BASE/python_modules/lsm/"
chrpath -d "$DEVEL_BASE/python_modules/lsm/$(basename "$_devel_clib_so")" \
    > /dev/null 2>&1 || true

# NFS plugin C extension (optional)
if [ -f "$_DEVEL_BUILD_DIR/plugin/nfs_plugin/.libs/nfs_clib.so" ]; then
    mkdir -p "$DEVEL_BASE/python_modules/lsm/plugin/nfs_plugin"
    cp "$_DEVEL_BUILD_DIR/plugin/nfs_plugin/.libs/nfs_clib.so" \
       "$DEVEL_BASE/python_modules/lsm/plugin/nfs_plugin/nfs_clib.so"
    chrpath -d \
        "$DEVEL_BASE/python_modules/lsm/plugin/nfs_plugin/nfs_clib.so" \
        > /dev/null 2>&1 || true
fi

# ---------------------------------------------------------------------------
# Install Python modules
# ---------------------------------------------------------------------------
# Core lsm package (from both source and build dirs — build has generated files
# like version.py)
find "$_DEVEL_SRC_DIR/python_binding/lsm/" -maxdepth 1 -type f -name '*.py' \
    -exec install {} "$DEVEL_BASE/python_modules/lsm/" \;
find "$_DEVEL_BUILD_DIR/python_binding/lsm/" -maxdepth 1 -type f -name '*.py' \
    -exec install {} "$DEVEL_BASE/python_modules/lsm/" \;

# lsmcli tool
find "$_DEVEL_SRC_DIR/tools/lsmcli/" -maxdepth 1 -type f -name '*.py' \
    -exec install {} "$DEVEL_BASE/python_modules/lsm/lsmcli/" \;
install "$_DEVEL_BUILD_DIR/tools/lsmcli/lsmcli" "$DEVEL_BASE/bin/lsmcli"

# ---------------------------------------------------------------------------
# Install Python plugins
# ---------------------------------------------------------------------------
cp -a "$_DEVEL_SRC_DIR"/plugin "$DEVEL_BASE/python_modules/lsm/"

# Copy the generated *_lsmplugin wrapper scripts into the plugins dir
find "$_DEVEL_BUILD_DIR"/plugin \( ! -regex ".*/\..*" \) \
    -name '*_lsmplugin' -exec install {} "$DEVEL_BASE/plugins/" \;

# Remove any C plugin libtool wrappers that accidentally got copied
# (when source dir == build dir the find above also picks up the libtool
# shell wrapper for simc_lsmplugin — keep only Python scripts)
for _p in "$DEVEL_BASE/plugins/"*; do
    [ -f "$_p" ] || continue
    if ! file -b "$_p" | grep -qi python; then
        rm -f "$_p"
    fi
done
unset _p

# ---------------------------------------------------------------------------
# Install C plugins (simc)
# ---------------------------------------------------------------------------
if [ -f "$_DEVEL_BUILD_DIR/plugin/simc/.libs/simc_lsmplugin" ]; then
    $_DEVEL_LIBTOOL --mode install \
        install "$_DEVEL_BUILD_DIR/plugin/simc/simc_lsmplugin" \
        "$DEVEL_BASE/plugins/" > /dev/null 2>&1
    chrpath -d "$DEVEL_BASE/plugins/simc_lsmplugin" > /dev/null 2>&1 || true
fi

# ---------------------------------------------------------------------------
# Install test programs (optional — only if built with --with-test)
# ---------------------------------------------------------------------------
if [ -f "$_DEVEL_BUILD_DIR/test/.libs/tester" ]; then
    $_DEVEL_LIBTOOL --mode install \
        install "$_DEVEL_BUILD_DIR/test/tester" \
        "$DEVEL_BASE/bin/tester" > /dev/null 2>&1
    chrpath -d "$DEVEL_BASE/bin/tester" > /dev/null 2>&1 || true
fi
for _ts in cmdtest.py plugin_test.py; do
    if [ -f "$_DEVEL_BUILD_DIR/test/$_ts" ]; then
        install "$_DEVEL_BUILD_DIR/test/$_ts" "$DEVEL_BASE/bin/$_ts"
    fi
done
unset _ts

# ---------------------------------------------------------------------------
# Config files
# ---------------------------------------------------------------------------
install "$_DEVEL_SRC_DIR/config/lsmd.conf" "$DEVEL_BASE/config/lsmd.conf"
find "$_DEVEL_SRC_DIR/config/pluginconf.d" -maxdepth 1 -type f -name '*.conf' \
    -exec install {} "$DEVEL_BASE/config/pluginconf.d/" \; 2>/dev/null || true

# ---------------------------------------------------------------------------
# Save original environment so devel_env_stop can restore them
# ---------------------------------------------------------------------------
_DEVEL_ORIG_PATH="$PATH"
_DEVEL_HAD_PYTHONPATH="${PYTHONPATH+yes}"
_DEVEL_ORIG_PYTHONPATH="${PYTHONPATH-}"
_DEVEL_HAD_LD_LIBRARY_PATH="${LD_LIBRARY_PATH+yes}"
_DEVEL_ORIG_LD_LIBRARY_PATH="${LD_LIBRARY_PATH-}"
_DEVEL_HAD_LSM_UDS_PATH="${LSM_UDS_PATH+yes}"
_DEVEL_ORIG_LSM_UDS_PATH="${LSM_UDS_PATH-}"
_DEVEL_HAD_LSM_SIM_DATA="${LSM_SIM_DATA+yes}"
_DEVEL_ORIG_LSM_SIM_DATA="${LSM_SIM_DATA-}"
_DEVEL_HAD_LSM_TEST_RUNDIR="${LSM_TEST_RUNDIR+yes}"
_DEVEL_ORIG_LSM_TEST_RUNDIR="${LSM_TEST_RUNDIR-}"
_DEVEL_HAD_LSM_TEST_CFG_DIR="${LSM_TEST_CFG_DIR+yes}"
_DEVEL_ORIG_LSM_TEST_CFG_DIR="${LSM_TEST_CFG_DIR-}"
_DEVEL_HAD_LSM_SIM_TIME="${LSM_SIM_TIME+yes}"
_DEVEL_ORIG_LSM_SIM_TIME="${LSM_SIM_TIME-}"
_DEVEL_HAD_LSMCLI_URI="${LSMCLI_URI+yes}"
_DEVEL_ORIG_LSMCLI_URI="${LSMCLI_URI-}"

# ---------------------------------------------------------------------------
# Export environment variables
# ---------------------------------------------------------------------------
export PYTHONPATH="$DEVEL_BASE/python_modules:$DEVEL_BASE/python_modules/lsm/plugin"
export LD_LIBRARY_PATH="$DEVEL_BASE/c_libs"
export LSM_UDS_PATH="$DEVEL_BASE/ipc"
export LSM_SIM_DATA="$DEVEL_BASE/plugin_data/lsm_sim_data"
export LSM_TEST_RUNDIR="$DEVEL_BASE/plugin_data/"
export LSM_TEST_CFG_DIR="$DEVEL_BASE/config/"
export LSM_SIM_TIME=0.1
export LSMCLI_URI="sim://"

# For the C unit test runner
export G_SLICE=always-malloc
export G_DEBUG=gc-friendly
export CK_DEFAULT_TIMEOUT=600
export CK_FORK=no

# Put our bin dir first on PATH
export PATH="$DEVEL_BASE/bin:$PATH"

# ---------------------------------------------------------------------------
# Start the daemon (-d = don't fork, we background it with &)
# ---------------------------------------------------------------------------
"$DEVEL_BASE/bin/lsmd" \
    --plugindir="$DEVEL_BASE/plugins" \
    --socketdir="$LSM_UDS_PATH" \
    --confdir="$DEVEL_BASE/config" \
    -d -v > "$DEVEL_BASE/logs/lsmd.log" 2>&1 &
DEVEL_LSMD_PID="$!"

sleep 2

if ! kill -0 "$DEVEL_LSMD_PID" 2>/dev/null; then
    DEVEL_LSMD_PID=""
fi
if [ -z "$DEVEL_LSMD_PID" ]; then
    echo "ERROR: Failed to start lsmd daemon."
    echo "       Check $DEVEL_BASE/logs/lsmd.log"
    cat "$DEVEL_BASE/logs/lsmd.log" 2>/dev/null
    if declare -f devel_env_stop > /dev/null 2>&1; then
        devel_env_stop
    else
        [ -d "$DEVEL_BASE" ] && { chmod +w -R "$DEVEL_BASE" 2>/dev/null || true; rm -rf "$DEVEL_BASE"; }
    fi
    unset DEVEL_BASE _devel_clib_so _DEVEL_SCRIPT_DIR _DEVEL_SRC_DIR _DEVEL_BUILD_DIR _DEVEL_LIBTOOL
    return 1 2>/dev/null || exit 1
fi

# ---------------------------------------------------------------------------
# Clean up internal variables
# ---------------------------------------------------------------------------
unset _devel_clib_so _DEVEL_SCRIPT_DIR _DEVEL_SRC_DIR _DEVEL_BUILD_DIR _DEVEL_LIBTOOL

# ---------------------------------------------------------------------------
# Provide cleanup and info functions
# ---------------------------------------------------------------------------
devel_env_stop() {
    if [ -n "$DEVEL_LSMD_PID" ]; then
        kill "$DEVEL_LSMD_PID" 2>/dev/null || true
        local _wait=0
        while kill -0 "$DEVEL_LSMD_PID" 2>/dev/null; do
            sleep 0.1
            _wait=$((_wait + 1))
            if [ "$_wait" -ge 50 ]; then
                kill -9 "$DEVEL_LSMD_PID" 2>/dev/null || true
                break
            fi
        done
        echo "Stopped lsmd (PID $DEVEL_LSMD_PID)"
        DEVEL_LSMD_PID=""
    fi
    if [ -d "$DEVEL_BASE" ]; then
        chmod +w -R "$DEVEL_BASE" 2>/dev/null || true
        rm -rf "$DEVEL_BASE"
        echo "Removed $DEVEL_BASE"
    fi
    unset DEVEL_BASE
    if [ -n "${_DEVEL_ORIG_PATH+set}" ]; then
        export PATH="$_DEVEL_ORIG_PATH"
        if [ "$_DEVEL_HAD_PYTHONPATH" = "yes" ]; then
            export PYTHONPATH="$_DEVEL_ORIG_PYTHONPATH"
        else
            unset PYTHONPATH
        fi
        if [ "$_DEVEL_HAD_LD_LIBRARY_PATH" = "yes" ]; then
            export LD_LIBRARY_PATH="$_DEVEL_ORIG_LD_LIBRARY_PATH"
        else
            unset LD_LIBRARY_PATH
        fi
        if [ "$_DEVEL_HAD_LSM_UDS_PATH" = "yes" ]; then
            export LSM_UDS_PATH="$_DEVEL_ORIG_LSM_UDS_PATH"
        else
            unset LSM_UDS_PATH
        fi
        if [ "$_DEVEL_HAD_LSM_SIM_DATA" = "yes" ]; then
            export LSM_SIM_DATA="$_DEVEL_ORIG_LSM_SIM_DATA"
        else
            unset LSM_SIM_DATA
        fi
        if [ "$_DEVEL_HAD_LSM_TEST_RUNDIR" = "yes" ]; then
            export LSM_TEST_RUNDIR="$_DEVEL_ORIG_LSM_TEST_RUNDIR"
        else
            unset LSM_TEST_RUNDIR
        fi
        if [ "$_DEVEL_HAD_LSM_TEST_CFG_DIR" = "yes" ]; then
            export LSM_TEST_CFG_DIR="$_DEVEL_ORIG_LSM_TEST_CFG_DIR"
        else
            unset LSM_TEST_CFG_DIR
        fi
        if [ "$_DEVEL_HAD_LSM_SIM_TIME" = "yes" ]; then
            export LSM_SIM_TIME="$_DEVEL_ORIG_LSM_SIM_TIME"
        else
            unset LSM_SIM_TIME
        fi
        if [ "$_DEVEL_HAD_LSMCLI_URI" = "yes" ]; then
            export LSMCLI_URI="$_DEVEL_ORIG_LSMCLI_URI"
        else
            unset LSMCLI_URI
        fi
        unset _DEVEL_ORIG_PATH \
              _DEVEL_HAD_PYTHONPATH _DEVEL_ORIG_PYTHONPATH \
              _DEVEL_HAD_LD_LIBRARY_PATH _DEVEL_ORIG_LD_LIBRARY_PATH \
              _DEVEL_HAD_LSM_UDS_PATH _DEVEL_ORIG_LSM_UDS_PATH \
              _DEVEL_HAD_LSM_SIM_DATA _DEVEL_ORIG_LSM_SIM_DATA \
              _DEVEL_HAD_LSM_TEST_RUNDIR _DEVEL_ORIG_LSM_TEST_RUNDIR \
              _DEVEL_HAD_LSM_TEST_CFG_DIR _DEVEL_ORIG_LSM_TEST_CFG_DIR \
              _DEVEL_HAD_LSM_SIM_TIME _DEVEL_ORIG_LSM_SIM_TIME \
              _DEVEL_HAD_LSMCLI_URI _DEVEL_ORIG_LSMCLI_URI
    fi
}

devel_env_show() {
    echo "===== libstoragemgmt development environment ====="
    echo "Base dir:        $DEVEL_BASE"
    echo "Daemon PID:      $DEVEL_LSMD_PID"
    echo "Daemon log:      $DEVEL_BASE/logs/lsmd.log"
    echo ""
    echo "PYTHONPATH:      $PYTHONPATH"
    echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
    echo "LSM_UDS_PATH:    $LSM_UDS_PATH"
    echo "LSMCLI_URI:      $LSMCLI_URI"
    echo ""
    echo "Installed plugins:"
    ls -1 "$DEVEL_BASE/plugins/"
    echo ""
    echo "Available commands:"
    echo "  lsmcli list --type systems    # talk to the simulator"
    echo "  LSMCLI_URI=simc:// lsmcli ... # use the C simulator"
    echo "  devel_env_show                # show this info again"
    echo "  devel_env_stop                # stop daemon and clean up"
    echo ""
    echo "Running tests:"
    echo "  tester                        # C unit tests (sim plugin)"
    echo "  tester use_simc               # C unit tests (simc plugin)"
    echo "  cmdtest.py -c lsmcli          # CLI tests"
    echo "  plugin_test.py -v             # plugin tests"
    echo "=================================================="
}

# ---------------------------------------------------------------------------
# Print summary
# ---------------------------------------------------------------------------
echo ""
devel_env_show
echo "Environment ready. Run 'devel_env_stop' when done."
