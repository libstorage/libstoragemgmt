# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Red Hat, Inc.
"""Black-box coverage for lsmd's per-uid admission and reaping path.

This runs from runtests.sh after the sim plug-in daemon has been started with
``max-connections-per-uid = 2``.  Keeping the first two sockets silent keeps
their workers alive.  The third connection must therefore be closed before a
worker is forked; the daemon log is checked to confirm that refusal, not
just the socket close, is what happened.  Once the first two clients go
away their workers exit, and the very next connection must be admitted:
lsmd has to reap before it judges a connection, not merely on its next idle
tick, or it refuses clients on the strength of workers that are long gone.
"""

import os
import socket
import struct
import sys
import time


# lsmd's SELECT_TIMEOUT_SECONDS: how long its event loop sleeps when no
# connection arrives, and so how long an exited worker can go unreaped.
LSMD_IDLE_TICK_SECONDS = 1

# Layout of struct ucred, as returned by SO_PEERCRED: pid, uid, gid.
UCRED_FORMAT = '3i'


def _connect(path):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(2)
    sock.connect(path)
    return sock


def _closed(sock):
    try:
        return sock.recv(1) == b''
    except socket.timeout:
        return False


def _lsmd_pid(sock):
    """The daemon's pid: SO_PEERCRED on a connected unix socket reports the
    process that was listening on it."""
    cred = sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED,
                           struct.calcsize(UCRED_FORMAT))
    return struct.unpack(UCRED_FORMAT, cred)[0]


def _exited_children(parent_pid):
    """Number of children of parent_pid that have exited but have not been
    reaped yet."""
    count = 0
    for entry in os.listdir('/proc'):
        if not entry.isdigit():
            continue
        try:
            with open('/proc/%s/stat' % entry) as stat_file:
                # The command name may contain spaces and parentheses; the
                # fields after its closing one are state and parent pid.
                fields = stat_file.read().rpartition(')')[2].split()
        except OSError:
            continue
        if fields[0] == 'Z' and int(fields[1]) == parent_pid:
            count += 1
    return count


def _log_path():
    return os.path.join(os.environ['LSM_TEST_LOG_DIR'], 'lsmd.log')


def _log_text():
    with open(_log_path()) as log_file:
        return log_file.read()


def main():
    path = os.path.join(os.environ['LSM_UDS_PATH'], 'sim')
    first = second = refused = replacement = None
    try:
        first = _connect(path)
        second = _connect(path)
        refused = _connect(path)
        if not _closed(refused):
            raise AssertionError('third connection was not refused at cap')
        refused.close()
        refused = None

        # The socket closing proves nothing about *why* on its own - an
        # unrelated bug that dropped every third connection would look
        # identical. Pin it to the admission path actually firing, and to
        # the point of the whole feature: the refused client never got as
        # far as exec_plugin(), so no plug-in process was forked for it.
        log = _log_text()
        refusals = log.count('Refusing connection from uid')
        execs = log.count("Exec'ing plug-in")
        if refusals != 1:
            raise AssertionError(
                'expected exactly 1 refusal logged, found %d' % refusals)
        if execs != 2:
            raise AssertionError(
                'expected exactly 2 plug-ins exec\'d before the refusal, '
                'found %d' % execs)

        lsmd_pid = _lsmd_pid(first)

        # Every pass of lsmd's event loop restarts its select() timeout, so
        # this (refused) connection guarantees the daemon will not wake on
        # its own for the next LSMD_IDLE_TICK_SECONDS.  Nothing but a new
        # connection can make it look at its children in that window.
        refused = _connect(path)
        if not _closed(refused):
            raise AssertionError('probe connection was not refused at cap')
        refused.close()
        refused = None
        quiet_until = time.monotonic() + LSMD_IDLE_TICK_SECONDS * 0.7

        first.close()
        second.close()
        first = second = None

        # Wait for both workers to notice the hang-up and exit.  They stay
        # zombies, as the daemon is asleep in select().
        while _exited_children(lsmd_pid) < 2 and \
                time.monotonic() < quiet_until:
            time.sleep(0.02)

        if time.monotonic() < quiet_until:
            # Both workers are gone and lsmd has not looked yet, so this
            # connection is judged on a stale count unless lsmd reaps
            # before admitting.  It must get in first time.
            replacement = _connect(path)
            if _closed(replacement):
                raise AssertionError(
                    'connection refused although both workers had exited: '
                    'exited workers were not reaped before admission')
            return 0

        # The workers were too slow to exit inside the quiet window (a
        # heavily loaded machine), so the ordering cannot be observed.
        # Settle for the weaker property that a slot reopens at all.
        print('note: workers exited too slowly to check reap-before-admit',
              file=sys.stderr)
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            replacement = _connect(path)
            if not _closed(replacement):
                return 0
            replacement.close()
            replacement = None
            time.sleep(0.1)
        raise AssertionError('a connection slot did not reopen after reaping')
    finally:
        for sock in (first, second, refused, replacement):
            if sock is not None:
                sock.close()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print('daemon connection-limit test failed: %s' % error,
              file=sys.stderr)
        sys.exit(1)
