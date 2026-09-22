# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.

import errno
import importlib.util
import os
import socket
import sys
import threading
import time
import types
import unittest

# Import python_binding/lsm/_pluginrunner.py (and the modules it needs)
# directly, bypassing lsm/__init__.py which pulls in the compiled _clib C
# extension.  Mirrors the approach in test_transport.py.
_lsm_src_dir = os.path.join(os.path.dirname(__file__), '..', 'python_binding',
                            'lsm')


def _load(name, filename):
    path = os.path.join(_lsm_src_dir, filename)
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


_lsm_pkg = types.ModuleType("lsm")
_lsm_pkg.__path__ = [_lsm_src_dir]
sys.modules.setdefault("lsm", _lsm_pkg)

_common = _load("lsm._common", "_common.py")
_load("lsm._data", "_data.py")
_transport = _load("lsm._transport", "_transport.py")

# _pluginrunner.py does "from lsm import LsmError, error, ErrorNumber", so the
# stub package must expose those names.
_lsm_pkg.LsmError = _common.LsmError
_lsm_pkg.error = _common.error
_lsm_pkg.ErrorNumber = _common.ErrorNumber

# It also does "from lsm.lsmcli import cmd_line_wrapper"; provide a stub so we
# don't drag in the CLI (and the C extension) it lives beside.
_lsmcli_stub = types.ModuleType("lsm.lsmcli")
_lsmcli_stub.cmd_line_wrapper = lambda *args, **kwargs: None
sys.modules["lsm.lsmcli"] = _lsmcli_stub
_lsm_pkg.lsmcli = _lsmcli_stub

_pluginrunner = _load("lsm._pluginrunner", "_pluginrunner.py")

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


REGISTER_PARAMS = {
    'uri': 'sim://',
    'plain_text_password': None,
    'timeout_ms': 1000,
    'flags': 0
}


def _plugin_class(register_delay=0, backend_error=None):
    """Builds a FakePlugin subclass for one test: plugin_register takes
    'register_delay' seconds, backend() raises 'backend_error', and
    'unregistered' is set once plugin_unregister has been called."""

    class Plugin(FakePlugin):
        unregistered = threading.Event()

        def plugin_register(self, **kwargs):
            time.sleep(register_delay)

        def plugin_unregister(self, **kwargs):
            Plugin.unregistered.set()

        def backend(self, **kwargs):
            raise backend_error

    return Plugin


class TestPluginRunner(unittest.TestCase):

    def setUp(self):
        # Small deadline so the pre-auth case fires quickly.
        self._saved_timeout = _pluginrunner.REGISTRATION_TIMEOUT
        _pluginrunner.REGISTRATION_TIMEOUT = 0.3
        # Capture what the runner logs instead of sending it to syslog.
        self._saved_error = _pluginrunner.error
        self.logged = []
        _pluginrunner.error = self.logged.append

    def tearDown(self):
        _pluginrunner.REGISTRATION_TIMEOUT = self._saved_timeout
        _pluginrunner.error = self._saved_error

    @staticmethod
    def _start_runner(plugin_sock, plugin=FakePlugin):
        runner = PluginRunner(
            plugin,
            ["fake_plugin", str(plugin_sock.fileno())])

        def run():
            try:
                runner.run()
            except SystemExit:
                # run() exits the process after an unclean shutdown; here
                # that only has to end the thread.
                pass

        thread = threading.Thread(target=run)
        thread.daemon = True
        thread.start()
        return thread

    def test_slow_register_still_succeeds(self):
        """The deadline is there for clients that do not register, not for
        arrays that are slow to log in.  A plugin_register that outlasts the
        deadline but succeeds has to be answered and the session has to carry
        on, as it does in the C plug-in runner."""
        plugin = _plugin_class(
            register_delay=_pluginrunner.REGISTRATION_TIMEOUT * 2)
        plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                     socket.SOCK_STREAM)
        client_sock.settimeout(5)
        try:
            thread = self._start_runner(plugin_sock, plugin)
            client = TransPort(client_sock)

            client.rpc('plugin_register', REGISTER_PARAMS)
            self.assertEqual(client.rpc('systems', {'flags': 0}), [])

            client.rpc('plugin_unregister', {'flags': 0})
            thread.join(timeout=3)
            self.assertFalse(thread.is_alive())
            self.assertEqual(self.logged, [])
        finally:
            plugin_sock.close()
            client_sock.close()

    def test_register_reply_failure_still_unregisters(self):
        """Once plugin_register has returned the plug-in holds whatever it
        logged in to.  A client that is gone by the time the reply is sent
        must not cost it the plugin_unregister that releases that."""
        plugin = _plugin_class(register_delay=0.1)
        plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                     socket.SOCK_STREAM)
        try:
            thread = self._start_runner(plugin_sock, plugin)
            TransPort(client_sock).send_req('plugin_register',
                                            REGISTER_PARAMS)
            client_sock.close()

            thread.join(timeout=3)
            self.assertFalse(thread.is_alive())
            self.assertTrue(
                plugin.unregistered.is_set(),
                "registered plug-in was dropped without plugin_unregister")
        finally:
            plugin_sock.close()
            client_sock.close()

    def test_backend_timeout_is_not_a_registration_timeout(self):
        """socket.timeout is the builtin TimeoutError, which a registered
        plug-in's own backend calls raise as well.  That is a plug-in bug to
        report to the client with a traceback in the log, not a client that
        failed to register."""
        for backend_error in (socket.timeout('backend timed out'),
                              TimeoutError('backend timed out'),
                              OSError(errno.EIO, 'backend i/o error')):
            del self.logged[:]
            plugin = _plugin_class(backend_error=backend_error)
            plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                         socket.SOCK_STREAM)
            client_sock.settimeout(5)
            try:
                thread = self._start_runner(plugin_sock, plugin)
                client = TransPort(client_sock)
                client.rpc('plugin_register', REGISTER_PARAMS)

                with self.assertRaises(_common.LsmError) as raised:
                    client.rpc('backend', {'flags': 0})
                self.assertEqual(raised.exception.code,
                                 _common.ErrorNumber.PLUGIN_BUG,
                                 repr(backend_error))

                thread.join(timeout=3)
                self.assertFalse(thread.is_alive())
                log = '\n'.join(self.logged)
                self.assertIn('Unhandled exception in plug-in', log)
                self.assertIn('Traceback', log)
                self.assertNotIn('failed to register', log)
                self.assertTrue(plugin.unregistered.is_set())
            finally:
                plugin_sock.close()
                client_sock.close()

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

            client.rpc('plugin_register', REGISTER_PARAMS)

            # Idle longer than the (now-cleared) pre-auth timeout.
            time.sleep(_pluginrunner.REGISTRATION_TIMEOUT * 2)

            result = client.rpc('systems', {
                'search_key': None,
                'search_value': None,
                'flags': 0
            })
            self.assertEqual(result, [],
                             "established session was killed during idle gap")

            client.rpc('plugin_unregister', {'flags': 0})
            thread.join(timeout=3)
            self.assertFalse(thread.is_alive(),
                             "plug-in did not exit after plugin_unregister")
        finally:
            plugin_sock.close()
            client_sock.close()


if __name__ == "__main__":
    unittest.main()
