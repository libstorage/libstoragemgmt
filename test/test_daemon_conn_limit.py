# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Red Hat, Inc.

"""Black-box coverage for lsmd's per-uid admission and reaping path.

This runs from runtests.sh after the sim plug-in daemon has been started with
``max-connections-per-uid = 2``.  Keeping the first two sockets silent keeps
their workers alive.  The third connection must therefore be closed before a
worker is forked; the daemon log is checked to confirm that refusal, not
just the socket close, is what happened.  Once the first two clients go
away, child_cleanup() must reap their workers and reopen a slot.
"""

import os
import socket
import sys
import time


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

        first.close()
        second.close()
        first = second = None

        # lsmd reaps at the select timeout cadence.  Give CI scheduling a
        # little slack while still pinning the one-second policy.
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            replacement = _connect(path)
            if not _closed(replacement):
                replacement.close()
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
