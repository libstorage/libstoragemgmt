# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.

import os
import socket
import sys
import threading
import time
import unittest

# Make the shared stub bootstrap importable however we're invoked (pytest
# from any directory, or running this file directly).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import lsm_stub

# Import python_binding/lsm/_pluginrunner.py (and the modules it needs)
# directly, bypassing lsm/__init__.py which pulls in the compiled _clib C
# extension.
lsm_stub.install()
_common = lsm_stub.load('lsm._common', '_common.py')
_transport = lsm_stub.load('lsm._transport', '_transport.py')
_pluginrunner = lsm_stub.load('lsm._pluginrunner', '_pluginrunner.py')

PluginRunner = _pluginrunner.PluginRunner
TransPort = _transport.TransPort


class FakePlugin(object):
    """Minimal plug-in exposing just the methods these tests drive.  All
    methods accept **kwargs because PluginRunner calls them with the request's
    params dict and return JSON-serializable results."""

    def plugin_register(self, **kwargs):
        return None

    def plugin_unregister(self, **kwargs):
        return None

    def systems(self, **kwargs):
        return []

    def bulk(self, **kwargs):
        # A reply large enough that a handful of them overflow the socket
        # buffer of a client that never reads.
        return 'x' * 65536


class TestPluginRunner(unittest.TestCase):
    def setUp(self):
        # Small deadline so the pre-auth case fires quickly.
        self._saved_timeout = _pluginrunner.REGISTRATION_TIMEOUT
        _pluginrunner.REGISTRATION_TIMEOUT = 0.3

    def tearDown(self):
        _pluginrunner.REGISTRATION_TIMEOUT = self._saved_timeout

    @staticmethod
    def _start_runner(plugin_sock):
        runner = PluginRunner(FakePlugin, ["fake_plugin",
                                           str(plugin_sock.fileno())])
        thread = threading.Thread(target=runner.run)
        thread.daemon = True
        thread.start()
        return thread

    def test_pre_auth_timeout_exits(self):
        """A client that connects but never sends plugin_register must not
        wedge the worker; run() has to exit once the pre-auth deadline
        fires."""
        plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                      socket.SOCK_STREAM)
        try:
            thread = self._start_runner(plugin_sock)
            # Send nothing at all from the client end.
            thread.join(timeout=3)
            self.assertFalse(
                thread.is_alive(),
                "plug-in did not exit after the pre-auth read timed out")
        finally:
            plugin_sock.close()
            client_sock.close()

    def test_pre_auth_chatter_does_not_extend_deadline(self):
        """The registration deadline is absolute.  A client that keeps the
        conversation going with requests the plug-in rejects must still be cut
        off at the original deadline; a per-message timeout would let it hold
        the worker (and its plug-in process) forever."""
        deadline = _pluginrunner.REGISTRATION_TIMEOUT
        plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                     socket.SOCK_STREAM)
        # Bound each client side read so a dead worker costs us one deadline,
        # not a hang.
        client_sock.settimeout(deadline)
        try:
            thread = self._start_runner(plugin_sock)
            client = TransPort(client_sock)

            start = time.monotonic()
            while thread.is_alive() and \
                    time.monotonic() - start < deadline * 6:
                try:
                    client.send_req('no_such_method', {'flags': 0})
                    client.read_resp()
                except _common.LsmError:
                    # The expected "unsupported operation" reply; keep going.
                    pass
                except (socket.timeout, OSError):
                    # Worker stopped answering, i.e. it gave up on us.
                    break
                time.sleep(deadline / 6)

            thread.join(timeout=2)
            elapsed = time.monotonic() - start
            self.assertFalse(
                thread.is_alive(),
                "plug-in kept running while an un-registered client chattered")
            self.assertLess(
                elapsed, deadline * 4,
                "registration deadline was renewed by each request "
                "(elapsed %.2fs, deadline %.2fs)" % (elapsed, deadline))
        finally:
            plugin_sock.close()
            client_sock.close()

    def test_pre_auth_deaf_client_does_not_pin_runner(self):
        """A client that keeps sending requests but never reads the replies
        fills the socket buffer; the runner has to give up rather than wedge
        inside sendall().  Note this does not discriminate the explicit send
        deadline on its own: _read_all() leaves a socket timeout behind that
        already bounded this path by accident.  It guards the end-to-end
        behavior the explicit check now makes deliberate."""
        deadline = _pluginrunner.REGISTRATION_TIMEOUT
        plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                     socket.SOCK_STREAM)
        # Bound our own sends too, so a wedged worker costs us seconds rather
        # than hanging the suite.
        client_sock.settimeout(5)
        try:
            thread = self._start_runner(plugin_sock)
            client = TransPort(client_sock)

            start = time.monotonic()
            try:
                for _ in range(8):
                    client.send_req('bulk', {'flags': 0})
            except (socket.timeout, OSError):
                pass

            # Not a single reply is ever read.
            thread.join(timeout=5)
            elapsed = time.monotonic() - start
            self.assertFalse(
                thread.is_alive(),
                "plug-in blocked sending replies the client never read")
            self.assertLess(
                elapsed, deadline * 3,
                "plug-in took far longer than the deadline to give up "
                "(elapsed %.2fs, deadline %.2fs)" % (elapsed, deadline))
        finally:
            plugin_sock.close()
            client_sock.close()

    def test_post_register_leniency(self):
        """After plugin_register the timeout must be cleared so an idle gap
        longer than the pre-auth timeout does not kill an established
        session."""
        plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                     socket.SOCK_STREAM)
        # Guard against a hang if the worker wrongly exits mid-session.
        client_sock.settimeout(5)
        try:
            thread = self._start_runner(plugin_sock)
            client = TransPort(client_sock)

            client.rpc('plugin_register', {'uri': 'sim://',
                                           'plain_text_password': None,
                                           'timeout_ms': 1000,
                                           'flags': 0})

            # Idle longer than the (now-cleared) pre-auth timeout.
            time.sleep(_pluginrunner.REGISTRATION_TIMEOUT * 2)

            result = client.rpc('systems', {'search_key': None,
                                            'search_value': None,
                                            'flags': 0})
            self.assertEqual(result, [],
                             "established session was killed during idle gap")

            client.rpc('plugin_unregister', {'flags': 0})
            thread.join(timeout=3)
            self.assertFalse(thread.is_alive(),
                             "plug-in did not exit after plugin_unregister")
        finally:
            plugin_sock.close()
            client_sock.close()


class TestRegistrationTimeoutFromEnv(unittest.TestCase):
    """_registration_timeout_from_env() is what lets lsmd.conf(5)'s
    "plugin-registration-timeout" reach a plug-in process: lsmd resolves it
    once at startup and passes it down via the environment (see
    LSM_PLUGIN_REGISTRATION_TIMEOUT_ENV in c_binding/lsm_ipc_timeout.h and
    the C-side mirror, registration_timeout_seconds(), in
    c_binding/lsm_plugin_ipc.cpp). Anything this parser gets wrong either
    silently ignores an administrator's override or - worse - disables the
    registration deadline outright."""

    ENV_NAME = _pluginrunner._REGISTRATION_TIMEOUT_ENV_NAME

    def setUp(self):
        self._had_env = self.ENV_NAME in os.environ
        self._saved_env = os.environ.get(self.ENV_NAME)

    def tearDown(self):
        if self._had_env:
            os.environ[self.ENV_NAME] = self._saved_env
        else:
            os.environ.pop(self.ENV_NAME, None)

    def _set(self, value):
        if value is None:
            os.environ.pop(self.ENV_NAME, None)
        else:
            os.environ[self.ENV_NAME] = value

    def test_unset_uses_default(self):
        self._set(None)
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_DEFAULT)

    def test_valid_override_is_used(self):
        self._set('120')
        self.assertEqual(_pluginrunner._registration_timeout_from_env(), 120)

    def test_empty_uses_default(self):
        self._set('')
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_DEFAULT)

    def test_zero_uses_default(self):
        # 0 is not a valid way to disable the deadline: an un-registered
        # client could then pin a worker forever, defeating the point.
        self._set('0')
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_DEFAULT)

    def test_negative_uses_default(self):
        self._set('-5')
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_DEFAULT)

    def test_non_numeric_uses_default(self):
        self._set('soon')
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_DEFAULT)

    def test_float_like_uses_default(self):
        # int() rejects "12.5" outright rather than truncating it; a typo'd
        # config value should fall back, not silently round down.
        self._set('12.5')
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_DEFAULT)

    def test_at_max_is_used(self):
        self._set(str(_pluginrunner._REGISTRATION_TIMEOUT_MAX))
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_MAX)

    def test_too_large_uses_default(self):
        # int() accepts arbitrarily large values, unlike C's strtol()+INT_MAX
        # check, so this has to be rejected explicitly - otherwise it would
        # sail through into set_io_deadline()'s time.monotonic() + seconds
        # as an uncaught OverflowError on every connection, rather than
        # falling back to the default the way an equally-malformed value
        # does in the C plug-in runner.
        self._set(str(_pluginrunner._REGISTRATION_TIMEOUT_MAX + 1))
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_DEFAULT)

    def test_absurdly_large_uses_default(self):
        self._set('9' * 400)
        self.assertEqual(_pluginrunner._registration_timeout_from_env(),
                         _pluginrunner._REGISTRATION_TIMEOUT_DEFAULT)


if __name__ == "__main__":
    unittest.main()
