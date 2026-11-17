# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Red Hat, Inc.
"""
Regression tests for de81a5ef ("pluginrunner: Restrict RPC dispatch to
public callable methods").

Before that fix, PluginRunner.run() dispatched any attribute name an RPC
client sent, as long as hasattr() was true and the result was called with
**params.  That let a client invoke a private helper never meant to be part
of the plug-in API, e.g. a static/instance method doing something like
running a shell command, simply by naming it as the RPC 'method'.

These tests are intentionally independent of the plugin_register deadline
feature (REGISTRATION_TIMEOUT / b8236c83, 33052fe6), which is not part of
the 1.10.3 backport: every request here is sent only after a successful
plugin_register, so this file has to stay correct whether or not that
feature exists.  Keeping it a separate module (and class) from
test_pluginrunner.py means it can be cherry-picked on its own.
"""

import os
import socket
import sys
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import lsm_stub

lsm_stub.install()
_common = lsm_stub.load('lsm._common', '_common.py')
_transport = lsm_stub.load('lsm._transport', '_transport.py')
_pluginrunner = lsm_stub.load('lsm._pluginrunner', '_pluginrunner.py')

PluginRunner = _pluginrunner.PluginRunner
TransPort = _transport.TransPort
LsmError = _common.LsmError
ErrorNumber = _common.ErrorNumber

REGISTER_PARAMS = {
    'uri': 'sim://',
    'plain_text_password': None,
    'timeout_ms': 1000,
    'flags': 0
}

# Method names that must never be reachable over RPC, each with the params
# (if any) a real attack would send.  Covers: a private helper that would
# run an external command, dunder methods that would reach back into the
# interpreter, and a leading-underscore name with no arguments at all.
REJECTED_CALLS = (
    ('_run_cmd', {
        'cmd': ['/bin/true'],
        'flags': 0
    }),
    ('__init__', {
        'flags': 0
    }),
    ('__class__', None),
    ('_private_helper', None),
)


class FakePlugin(object):
    """Plug-in stand-in with one legitimate public method and the private
    helpers an RPC client must not be able to reach."""

    ran_private = threading.Event()

    def plugin_register(self, **kwargs):
        return None

    def plugin_unregister(self, **kwargs):
        return None

    def systems(self, **kwargs):
        return []

    def _run_cmd(self, cmd=None, **kwargs):
        # Stands in for the real plug-ins' internal command-runner helper;
        # setting the flag is the proof an attacker reached it.
        FakePlugin.ran_private.set()
        return 'ran: %s' % cmd

    def _private_helper(self, **kwargs):
        FakePlugin.ran_private.set()
        return 'private'


class NonCallableAttr(FakePlugin):
    """A plug-in whose 'not_callable' attribute exists (hasattr is True)
    but is plain data, not a method - the other half of the fix: checking
    callable() in addition to rejecting '_'-prefixed names."""
    not_callable = "surprise"


class TestRpcDispatchRestriction(unittest.TestCase):
    """RPC dispatch must reject non-public / non-callable targets and leave
    legitimate public methods working."""

    def setUp(self):
        FakePlugin.ran_private.clear()
        self._saved_error = _pluginrunner.error
        self.logged = []
        _pluginrunner.error = self.logged.append

    def tearDown(self):
        _pluginrunner.error = self._saved_error

    @staticmethod
    def _start_runner(plugin_sock, plugin):
        runner = PluginRunner(
            plugin, ["fake_plugin", str(plugin_sock.fileno())])

        def run():
            try:
                runner.run()
            except SystemExit:
                pass

        thread = threading.Thread(target=run)
        thread.daemon = True
        thread.start()
        return thread

    def _registered_client(self, plugin=FakePlugin):
        plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                     socket.SOCK_STREAM)
        client_sock.settimeout(5)
        thread = self._start_runner(plugin_sock, plugin)
        client = TransPort(client_sock)
        client.rpc('plugin_register', REGISTER_PARAMS)
        return thread, client, plugin_sock, client_sock

    def _finish(self, thread, client, plugin_sock, client_sock):
        try:
            client.rpc('plugin_unregister', {'flags': 0})
            thread.join(timeout=3)
        finally:
            plugin_sock.close()
            client_sock.close()

    def test_underscore_and_dunder_methods_rejected(self):
        """Every name starting with '_' is refused with NO_SUPPORT and the
        target itself is never invoked, whether or not it exists."""
        thread, client, plugin_sock, client_sock = self._registered_client()
        try:
            for method, params in REJECTED_CALLS:
                with self.assertRaises(LsmError) as raised:
                    client.rpc(method, params)
                self.assertEqual(raised.exception.code, ErrorNumber.NO_SUPPORT,
                                 method)
            self.assertFalse(FakePlugin.ran_private.is_set(),
                             "a rejected RPC method name reached the plug-in")
        finally:
            self._finish(thread, client, plugin_sock, client_sock)

    def test_non_callable_attribute_rejected(self):
        """A public-looking attribute that resolves via getattr() but is not
        callable (e.g. plain data) must be refused rather than called."""
        thread, client, plugin_sock, client_sock = self._registered_client(
            plugin=NonCallableAttr)
        try:
            with self.assertRaises(LsmError) as raised:
                client.rpc('not_callable', {'flags': 0})
            self.assertEqual(raised.exception.code, ErrorNumber.NO_SUPPORT)
        finally:
            self._finish(thread, client, plugin_sock, client_sock)

    def test_non_string_method_rejected(self):
        """A method name that is not even a string (e.g. an int or a list,
        smuggled through a hand-built JSON-RPC request) must be rejected
        the same way, not raise an unrelated TypeError inside getattr()."""
        plugin_sock, client_sock = socket.socketpair(socket.AF_UNIX,
                                                     socket.SOCK_STREAM)
        client_sock.settimeout(5)
        thread = self._start_runner(plugin_sock, FakePlugin)
        client = TransPort(client_sock)
        try:
            client.rpc('plugin_register', REGISTER_PARAMS)
            for bad_method in (1234, ['systems'], None):
                client.send_req(bad_method, {'flags': 0})
                with self.assertRaises(LsmError) as raised:
                    client.read_resp()
                self.assertEqual(raised.exception.code, ErrorNumber.NO_SUPPORT,
                                 repr(bad_method))
        finally:
            self._finish(thread, client, plugin_sock, client_sock)

    def test_public_method_still_dispatches(self):
        """The restriction must not collaterally break legitimate public
        RPC calls."""
        thread, client, plugin_sock, client_sock = self._registered_client()
        try:
            self.assertEqual(client.rpc('systems', {'flags': 0}), [])
        finally:
            self._finish(thread, client, plugin_sock, client_sock)


if __name__ == "__main__":
    unittest.main()
